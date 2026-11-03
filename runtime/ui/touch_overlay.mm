// On-screen controls for a touch screen: on by default on Android (a phone with no controller has no other way to
// play), elsewhere only with LARECOMP_TOUCH=1. Drawn over the game by menu_render (menu.mm) and fed by
// runtime/hle/input.c, which adds its buttons and sticks to the keyboard's and the controller's.
//
// Layout, all in units of the screen's height so it scales to any phone: a left stick that floats - wherever a thumb
// lands in the lower left becomes its centre -, A/B/X/Y in the Switch's diamond on the right with the D-pad above the
// stick, L/ZL and R/ZR in the top corners, minus / menu / plus at the top centre. A drag on the right half away from
// the buttons is the right stick (the camera). A finger sliding from one button to the next presses the next, as on a
// real pad; a press shorter than a game poll still reaches the game (input.c note_press). The overlay hides as soon as
// a physical controller is used and comes back with the next touch. The menu button does what F1 does.
//
// Threads: SDL events (touch_overlay_event) on the main thread, drawing on the render thread, the game's controller
// reads (touch_overlay_buttons / _sticks) on the game's: the state they share is atomics, the finger table is the
// event thread's alone.
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "../input_bind.h"  // BIND_A..BIND_DOWN: the Switch's button bits
#include "imgui.h"

extern "C" void menu_toggle_from_touch(void);  // menu.mm: open / close the menu, as F1

namespace {

enum Kind { K_NONE, K_BUTTON, K_MENU, K_LSTICK, K_RSTICK };

struct Btn {
    float x, y, r;  // centre and radius, pixels
    int bind;       // BIND_*, or -1 for the menu button
    const char *label;
    bool pill;      // a shoulder button: drawn wider
};

enum { MAX_BTN = 20, MAX_FINGERS = 10 };

struct Layout {
    Btn b[MAX_BTN];
    int n;
    float stick_x, stick_y, stick_r;  // the left stick's resting place and reach
    float w, h;
};

// The same function of the screen size on every thread, so the events and the drawing always agree.
void make_layout(float w, float h, Layout *L) {
    float u = h, n = 0;
    L->w = w, L->h = h, L->n = 0;
    auto add = [&](float x, float y, float r, int bind, const char *label, bool pill) {
        if (L->n < MAX_BTN) L->b[L->n++] = {x, y, r, bind, label, pill};
    };
    (void)n;
    float fx = w - 0.25f * u, fy = h - 0.30f * u, fd = 0.115f * u, fr = 0.068f * u;  // face buttons: Switch layout
    add(fx + fd, fy, fr, BIND_A, "A", false);
    add(fx, fy + fd, fr, BIND_B, "B", false);
    add(fx, fy - fd, fr, BIND_X, "X", false);
    add(fx - fd, fy, fr, BIND_Y, "Y", false);
    float dx = 0.22f * u, dy = h - 0.66f * u, dd = 0.072f * u, dr = 0.046f * u;  // D-pad, above the stick
    add(dx, dy - dd, dr, BIND_UP, "", false);
    add(dx, dy + dd, dr, BIND_DOWN, "", false);
    add(dx - dd, dy, dr, BIND_LEFT, "", false);
    add(dx + dd, dy, dr, BIND_RIGHT, "", false);
    float sy = 0.17f * u, sr = 0.055f * u;  // shoulders (below the FPS counter's corner)
    add(0.10f * u, sy, sr, BIND_ZL, "ZL", true);
    add(0.27f * u, sy, sr, BIND_L, "L", true);
    add(w - 0.27f * u, sy, sr, BIND_R, "R", true);
    add(w - 0.10f * u, sy, sr, BIND_ZR, "ZR", true);
    float cr = 0.040f * u;  // minus, menu, plus
    add(w * 0.5f - 0.13f * u, 0.075f * u, cr, BIND_MINUS, "-", false);
    add(w * 0.5f, 0.075f * u, cr, -1, "", false);
    add(w * 0.5f + 0.13f * u, 0.075f * u, cr, BIND_PLUS, "+", false);
    L->stick_x = 0.22f * u, L->stick_y = h - 0.27f * u, L->stick_r = 0.15f * u;
}

int hit(const Layout &L, float x, float y, bool face_only) {
    int best = -1;
    float bd = 0;
    for (int i = 0; i < L.n; i++) {
        const Btn &b = L.b[i];
        if (face_only && (b.bind < 0 || b.pill || b.bind == BIND_PLUS || b.bind == BIND_MINUS)) continue;
        float d = hypotf(x - b.x, y - b.y), reach = b.r * (b.pill ? 1.6f : 1.3f);
        if (d <= reach && (best < 0 || d < bd)) best = i, bd = d;
    }
    return best;
}

struct Finger {
    SDL_FingerID id;
    bool used;
    int kind, btn;
    float ox, oy, x, y;  // where it went down, where it is (pixels)
};

// event thread only
Finger fingers[MAX_FINGERS];

// shared
std::atomic<int> g_enabled{-1};
std::atomic<bool> g_hidden{false};          // a physical controller was used since the last touch
std::atomic<uint64_t> g_last_draw_ns{0};    // drawn lately = the game is on screen with the menu closed
std::atomic<uint32_t> g_w{0}, g_h{0};       // the screen size the layout is made for (from the last draw)
std::atomic<uint64_t> g_buttons{0};
std::atomic<int32_t> g_lx{0}, g_ly{0}, g_rx{0}, g_ry{0};
std::atomic<float> g_lox{0}, g_loy{0};      // the left stick's centre while held (it floats)
std::atomic<bool> g_lheld{false}, g_release{false};

bool enabled() {
    int e = g_enabled.load();
    if (e < 0) {
        const char *v = getenv("LARECOMP_TOUCH");
#ifdef __ANDROID__
        e = !(v && !strcmp(v, "0"));
#else
        e = v && strcmp(v, "0") != 0;
#endif
        g_enabled = e;
    }
    return e;
}

int32_t axis(float v) { return (int32_t)lroundf(fmaxf(-1.0f, fminf(1.0f, v)) * 32767.0f); }

void recompute(const Layout &L) {
    uint64_t b = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    bool lheld = false;
    for (const Finger &f : fingers) {
        if (!f.used) continue;
        if (f.kind == K_BUTTON && f.btn >= 0 && L.b[f.btn].bind >= 0) b |= 1ull << L.b[f.btn].bind;
        float vx = (f.x - f.ox) / L.stick_r, vy = (f.y - f.oy) / L.stick_r, m = hypotf(vx, vy);
        if (m > 1) vx /= m, vy /= m;
        if (f.kind == K_LSTICK) lx = axis(vx), ly = axis(-vy), lheld = true, g_lox = f.ox, g_loy = f.oy;
        if (f.kind == K_RSTICK) rx = axis(vx * 1.25f), ry = axis(-vy * 1.25f);
    }
    g_buttons = b, g_lx = lx, g_ly = ly, g_rx = rx, g_ry = ry, g_lheld = lheld;
}

void clear_fingers() {
    memset(fingers, 0, sizeof fingers);
    g_buttons = 0, g_lx = g_ly = g_rx = g_ry = 0, g_lheld = false;
}

}  // namespace

extern "C" bool touch_overlay_enabled(void) { return enabled(); }
extern "C" bool touch_overlay_wanted(void) { return enabled() && !g_hidden.load(); }
extern "C" uint64_t touch_overlay_buttons(void) { return g_buttons.load(); }
extern "C" void touch_overlay_sticks(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    *lx = g_lx.load(), *ly = g_ly.load(), *rx = g_rx.load(), *ry = g_ry.load();
}

// A physical controller pressed or pushed: the overlay steps aside (input.c).
extern "C" void touch_overlay_pad_used(void) {
    if (!enabled() || g_hidden.load()) return;
    g_hidden = true;
    g_release = true;
    g_buttons = 0, g_lx = g_ly = g_rx = g_ry = 0;
}

// Not on screen (the menu open, the start screen, a controller in use): whatever was held lets go (menu.mm).
extern "C" void touch_overlay_release(void) {
    if (g_buttons.load() || g_lx.load() || g_ly.load() || g_rx.load() || g_ry.load()) {
        g_buttons = 0, g_lx = g_ly = g_rx = g_ry = 0;
    }
    g_release = true;
}

// SDL finger events (main thread). true: it was one - input.c turns the change in buttons into presses.
extern "C" bool touch_overlay_event(const SDL_Event *e) {
    if (e->type != SDL_EVENT_FINGER_DOWN && e->type != SDL_EVENT_FINGER_MOTION && e->type != SDL_EVENT_FINGER_UP &&
        e->type != SDL_EVENT_FINGER_CANCELED)
        return false;
    if (!enabled()) return true;
    if (g_release.exchange(false)) clear_fingers();
    if (e->type == SDL_EVENT_FINGER_DOWN && g_hidden.load()) g_hidden = false;  // a touch brings it back
    // only while the game is on screen with the menu closed (menu_render drew it within the last quarter second)
    uint64_t now = SDL_GetTicksNS(), last = g_last_draw_ns.load();
    float w = (float)g_w.load(), h = (float)g_h.load();
    if (!last || now - last > 250000000ull || w <= 0 || h <= 0) { clear_fingers(); return true; }
    Layout L;
    make_layout(w, h, &L);
    float x = e->tfinger.x * w, y = e->tfinger.y * h;
    Finger *f = NULL;
    for (Finger &g : fingers)
        if (g.used && g.id == e->tfinger.fingerID) f = &g;
    if (e->type == SDL_EVENT_FINGER_DOWN) {
        if (!f)
            for (Finger &g : fingers)
                if (!g.used) { f = &g; break; }
        if (!f) return true;  // (more fingers than anyone has)
        *f = {e->tfinger.fingerID, true, K_NONE, -1, x, y, x, y};
        int b = hit(L, x, y, false);
        if (b >= 0 && L.b[b].bind < 0) {
            f->kind = K_MENU;
            menu_toggle_from_touch();
        } else if (b >= 0) {
            f->kind = K_BUTTON, f->btn = b;
        } else if (x < w * 0.45f && y > h * 0.40f) {
            f->kind = K_LSTICK;
            if (hypotf(x - L.stick_x, y - L.stick_y) <= L.stick_r) f->ox = L.stick_x, f->oy = L.stick_y;  // (on its base)
        } else if (x > w * 0.5f) {
            f->kind = K_RSTICK;
        }
    } else if (f && e->type == SDL_EVENT_FINGER_MOTION) {
        f->x = x, f->y = y;
        if (f->kind == K_BUTTON && !L.b[f->btn].pill) {  // slide between face buttons / the D-pad
            int b = hit(L, x, y, true);
            f->btn = b >= 0 ? b : f->btn;
            if (b < 0 && hypotf(x - L.b[f->btn].x, y - L.b[f->btn].y) > L.b[f->btn].r * 2.2f) f->kind = K_NONE;
        }
    } else if (f) {  // up / cancelled
        f->used = false;
    }
    recompute(L);
    return true;
}

// The render thread, inside menu_render's ImGui frame, on top of the game.
extern "C" void touch_overlay_draw(ImVec2 display, float scale) {
    (void)scale;
    g_w = (uint32_t)display.x, g_h = (uint32_t)display.y;
    g_last_draw_ns = SDL_GetTicksNS();
    Layout L;
    make_layout(display.x, display.y, &L);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *font = ImGui::GetFont();
    uint64_t held = g_buttons.load();
    const ImU32 fill = IM_COL32(255, 255, 255, 46), fill_on = IM_COL32(255, 255, 255, 140);
    const ImU32 edge = IM_COL32(255, 255, 255, 120), text = IM_COL32(255, 255, 255, 210);
    for (int i = 0; i < L.n; i++) {
        const Btn &b = L.b[i];
        bool on = b.bind >= 0 && (held >> b.bind & 1);
        ImVec2 c(b.x, b.y);
        if (b.pill) {
            ImVec2 a(b.x - b.r * 1.5f, b.y - b.r * 0.75f), z(b.x + b.r * 1.5f, b.y + b.r * 0.75f);
            dl->AddRectFilled(a, z, on ? fill_on : fill, b.r * 0.75f);
            dl->AddRect(a, z, edge, b.r * 0.75f, 0, 2.0f);
        } else {
            dl->AddCircleFilled(c, b.r, on ? fill_on : fill, 32);
            dl->AddCircle(c, b.r, edge, 32, 2.0f);
        }
        if (b.bind < 0) {  // the menu button: three bars
            for (int k = -1; k <= 1; k++)
                dl->AddLine(ImVec2(b.x - b.r * 0.45f, b.y + k * b.r * 0.32f), ImVec2(b.x + b.r * 0.45f, b.y + k * b.r * 0.32f),
                            text, 2.5f);
        } else if (b.bind == BIND_MINUS || b.bind == BIND_PLUS) {  // bars: the font's hyphen was too thin to see
            float s = b.r * 0.42f, t = fmaxf(2.0f, b.r * 0.09f);
            dl->AddLine(ImVec2(b.x - s, b.y), ImVec2(b.x + s, b.y), text, t);
            if (b.bind == BIND_PLUS) dl->AddLine(ImVec2(b.x, b.y - s), ImVec2(b.x, b.y + s), text, t);
        } else if (b.bind >= BIND_LEFT && b.bind <= BIND_DOWN) {  // a D-pad arrow
            float s = b.r * 0.42f;
            ImVec2 p0, p1, p2;
            if (b.bind == BIND_UP) p0 = {b.x, b.y - s}, p1 = {b.x - s, b.y + s * 0.6f}, p2 = {b.x + s, b.y + s * 0.6f};
            else if (b.bind == BIND_DOWN) p0 = {b.x, b.y + s}, p1 = {b.x + s, b.y - s * 0.6f}, p2 = {b.x - s, b.y - s * 0.6f};
            else if (b.bind == BIND_LEFT) p0 = {b.x - s, b.y}, p1 = {b.x + s * 0.6f, b.y + s}, p2 = {b.x + s * 0.6f, b.y - s};
            else p0 = {b.x + s, b.y}, p1 = {b.x - s * 0.6f, b.y - s}, p2 = {b.x - s * 0.6f, b.y + s};
            dl->AddTriangleFilled(p0, p1, p2, text);
        } else if (b.label[0]) {
            float size = b.r * (b.pill ? 0.8f : 0.95f);
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, b.label);
            dl->AddText(font, size, ImVec2(b.x - ts.x * 0.5f, b.y - ts.y * 0.5f), text, b.label);
        }
    }
    // the left stick: at rest on its base, or where the thumb went down
    bool lh = g_lheld.load();
    ImVec2 base = lh ? ImVec2(g_lox.load(), g_loy.load()) : ImVec2(L.stick_x, L.stick_y);
    dl->AddCircleFilled(base, L.stick_r, IM_COL32(255, 255, 255, 28), 48);
    dl->AddCircle(base, L.stick_r, edge, 48, 2.0f);
    ImVec2 knob(base.x + g_lx.load() / 32767.0f * L.stick_r, base.y - g_ly.load() / 32767.0f * L.stick_r);
    dl->AddCircleFilled(knob, L.stick_r * 0.42f, lh ? fill_on : IM_COL32(255, 255, 255, 70), 32);
}
