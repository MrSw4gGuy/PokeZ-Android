// Touch gestures for our ImGui pages (the start screen, the F1 menu): Android sends a finger as the mouse
// (SDL_TOUCH_MOUSEID), pressing the instant it lands - an option changed by the finger that only meant to scroll, and
// a list couldn't be scrolled at all (ImGui scrolls by the wheel and the scrollbar). Here a touch becomes either:
//   a tap: lifted within UI_TOUCH_SLOP of where it landed - a press and a release there, on the lift;
//   a drag: moved farther - the page scrolls with the finger (wheel steps), and nothing is clicked.
// ui_touch_filter turns one SDL mouse event into the events to hand on (out, up to 3; the count is returned); not a
// touch: the event itself. Main thread only (the state is a static). Desktop builds: every event passes as it is.
#pragma once
#include <SDL3/SDL.h>
#include <math.h>

static inline int ui_touch_filter(const SDL_Event *e, SDL_Event *out, float wheel_px) {
#ifdef __ANDROID__
    static bool down, dragging;
    static float x0, y0, ly;
    bool touch = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) ? e->button.which == SDL_TOUCH_MOUSEID
               : e->type == SDL_EVENT_MOUSE_MOTION ? e->motion.which == SDL_TOUCH_MOUSEID : false;
    if (!touch) { out[0] = *e; return 1; }
    const float slop = 24;  // (pixels: a finger's wobble on a tap)
    switch (e->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button != SDL_BUTTON_LEFT) return 0;
        down = true, dragging = false, x0 = e->button.x, y0 = ly = e->button.y;
        SDL_zero(out[0]);
        out[0].type = SDL_EVENT_MOUSE_MOTION, out[0].motion.which = SDL_TOUCH_MOUSEID;
        out[0].motion.windowID = e->button.windowID, out[0].motion.x = e->button.x, out[0].motion.y = e->button.y;
        return 1;  // (the cursor there - hover - but no press yet)
    case SDL_EVENT_MOUSE_MOTION:
        if (!down) { out[0] = *e; return 1; }
        if (!dragging && hypotf(e->motion.x - x0, e->motion.y - y0) > slop) dragging = true;
        if (dragging) {  // (the page follows the finger: wheel up for a finger moving down)
            float dy = e->motion.y - ly;
            ly = e->motion.y;
            SDL_zero(out[0]);
            out[0].type = SDL_EVENT_MOUSE_WHEEL, out[0].wheel.which = SDL_TOUCH_MOUSEID;
            out[0].wheel.windowID = e->motion.windowID, out[0].wheel.y = dy / wheel_px;
            out[0].wheel.mouse_x = e->motion.x, out[0].wheel.mouse_y = e->motion.y;
            return 1;
        }
        return 0;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button != SDL_BUTTON_LEFT || !down) return 0;
        down = false;
        if (dragging) return 0;
        out[0] = *e, out[1] = *e;  // a tap: press and release where it landed
        out[0].type = SDL_EVENT_MOUSE_BUTTON_DOWN, out[0].button.down = true, out[0].button.x = x0, out[0].button.y = y0;
        out[1].button.x = x0, out[1].button.y = y0;
        return 2;
    default: break;
    }
#else
    (void)wheel_px;
#endif
    out[0] = *e;
    return 1;
}

// The UI's size on a phone: our pages are laid out for a monitor, where 1080 lines are a hand-span tall; a phone's are
// ~7 cm - text at the desktop's size was too small to read. Android: everything 2.2x. 1 elsewhere.
static inline float ui_touch_scale(void) {
#ifdef __ANDROID__
    return 2.2f;
#else
    return 1.0f;
#endif
}
