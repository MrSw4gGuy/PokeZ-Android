// Android: which Vulkan driver the runtime talks to. libmain.so carries every Vulkan command itself (tools/gen_vk_shim.py,
// built into $(B)/inc/vk_shim.c) and links no libvulkan.so; each command is looked up here, once, through the
// vkGetInstanceProcAddr of the loader opened below.
//   - Default: the system's loader and Qualcomm's driver, as before (the same libvulkan.so a -lvulkan link gave).
//   - A custom driver (e.g. a Turnip build, Mesa's open Adreno driver, as emulators load it): F1 > Video > GPU driver
//     (g_settings.vk_driver), or LARECOMP_VK_DRIVER=<name> in env.txt over it (a test run; "system": the system's).
//     Drivers live in <data root>/drivers/, so a player can add one this app doesn't ship (a newer Turnip, another
//     vendor build): a package folder <name>/ holding meta.json and the library it names - the layout of the zips
//     emulators share (K11MCH1's AdrenoToolsDrivers: {"name", "libraryName", ...}), imported from the menu
//     (plat_vk_driver_import) - or a bare .so put there by hand. The chosen one is copied into the app's internal storage
//     (an app cannot load code from shared storage) and libadrenotools (third_party/libadrenotools, BSD-2-Clause) opens a
//     second copy of the system's loader in a linker namespace of its own, whose driver is that file. The loader still
//     makes the surface and the swapchain over the driver's VK_ANDROID_native_buffer. Its hook libraries ship in the
//     APK's lib folder (make_project.sh). A driver that does not load leaves the system's, logged.
//   - A driver that loads but kills the process bringing the GPU up would do so at every start, the start screen's too
//     (it runs on the same device, so its menu could not undo the choice): the settings file names it as on trial
//     (vk_driver_trial) until vulkan/device.c ui_window_init has the device and swapchain (plat_vk_driver_up). A start
//     that finds a trial left over goes back to the system's driver and says so in the menu (plat_vk_driver_failed).
// Another port measured Turnip on an Adreno 830 at GPU 16-17 ms a frame against 22-27 ms on Qualcomm's driver in the same
// scene (nx2android's gfx_vk_core.c, TOTKDroid); ours: G-buffer 4.1-4.6 ms against 10.7-12.4 ms (docs/android-port.md).
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <adrenotools/driver.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../platform.h"
#include "../rt.h"
#include "../settings.h"

static PFN_vkGetInstanceProcAddr g_gipa;
static VkInstance g_inst;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static char g_loaded[96];  // the package this process runs on ("": the system's driver)
static char g_failed[96];  // the chosen one this start didn't run on (the last start died with it, or it didn't load)
static bool g_trial;       // this start put the chosen one on trial (cleared by plat_vk_driver_up)

static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static bool ends_with_so(const char *n) { size_t l = strlen(n); return l > 3 && !strcmp(n + l - 3, ".so"); }

// A JSON string value by key, as meta.json files hold them (flat, one level: no full parser needed); false: none
static bool json_str(const char *js, const char *key, char *out, size_t n) {
    char pat[80];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"') return false;
    size_t k = 0;
    for (; *p && *p != '"' && k + 1 < n; p++) {
        if (*p == '\\' && p[1]) p++;  // (\" \\ \/: the character itself; names hold nothing fancier)
        out[k++] = *p;
    }
    out[k] = 0;
    return k > 0;
}

// a folder name from a driver's name: no path separators or characters a file manager chokes on
static void safe_name(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (; *in && k + 1 < n; in++) {
        unsigned char c = (unsigned char)*in;
        out[k++] = c < 0x20 || strchr("/\\:*?\"<>|", c) ? '_' : (char)c;
    }
    while (k && (out[k - 1] == ' ' || out[k - 1] == '.')) k--;  // (no trailing dots / spaces)
    out[k] = 0;
    if (!k || !strcmp(out, ".") || !strcmp(out, "..")) snprintf(out, n, "driver");
}

// <data root>/drivers/<name>: the library to load (folder + file name), false if it isn't a driver
static bool driver_lib(const char *name, char *dir, size_t dn, char *lib, size_t ln) {
    const char *root = getenv("LARECOMP_DATA_ROOT");
    if (!root || !*name || strchr(name, '/') || name[0] == '.') return false;
    char p[1024];
    snprintf(p, sizeof p, "%s/drivers/%s", root, name);
    if (!is_dir(p)) {  // a bare .so in drivers/ (put there by hand, as LARECOMP_VK_DRIVER first took them)
        if (!ends_with_so(name) || access(p, R_OK) != 0) return false;
        snprintf(dir, dn, "%s/drivers", root);
        snprintf(lib, ln, "%s", name);
        return true;
    }
    snprintf(dir, dn, "%s", p);
    lib[0] = 0;
    char mp[1100];
    snprintf(mp, sizeof mp, "%s/meta.json", p);
    size_t sz = 0;
    char *js = SDL_LoadFile(mp, &sz);
    if (js) {
        char l[256];
        if (json_str(js, "libraryName", l, sizeof l) && !strchr(l, '/')) snprintf(lib, ln, "%s", l);
        SDL_free(js);
    }
    DIR *d = lib[0] ? NULL : opendir(p);  // (no meta.json: the folder's .so)
    if (d) {
        for (struct dirent *e; !lib[0] && (e = readdir(d));)
            if (ends_with_so(e->d_name)) snprintf(lib, ln, "%s", e->d_name);
        closedir(d);
    }
    char lp[1400];
    snprintf(lp, sizeof lp, "%s/%s", p, lib);
    return lib[0] && access(lp, R_OK) == 0;
}

// The package's library -> <internal>/gpu_drivers/<name>/<lib>, copied again only when the size differs. Returns the
// internal folder with its trailing slash (adrenotools joins folder and name as they are), or NULL.
static const char *driver_copy(const char *name, char *dir, size_t n, char *lib, size_t ln) {
    const char *base = SDL_GetAndroidInternalStoragePath();
    char sdir[1024];
    if (!base) return NULL;
    snprintf(sdir, sizeof sdir, "%s/vkdriver", base);  // (the flat copies before 2026-11-11's packages: not read any more)
    if (is_dir(sdir)) plat_remove_tree(sdir);
    if (!driver_lib(name, sdir, sizeof sdir, lib, ln)) {
        LOG("[vk] driver %s: not in drivers/ (or no library in it)\n", name);
        return NULL;
    }
    char src[1400], dst[1400];
    snprintf(src, sizeof src, "%s/%s", sdir, lib);
    snprintf(dir, n, "%s/gpu_drivers", base);
    mkdir(dir, 0755);
    snprintf(dir, n, "%s/gpu_drivers/%s/", base, name);
    mkdir(dir, 0755);
    snprintf(dst, sizeof dst, "%s%s", dir, lib);
    struct stat ss, ds;
    if (stat(src, &ss) != 0) return NULL;
    if (stat(dst, &ds) == 0 && ds.st_size == ss.st_size) return dir;
    int in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    bool ok = in >= 0 && out >= 0;
    char buf[1 << 16];
    for (ssize_t r; ok && (r = read(in, buf, sizeof buf)) > 0;) ok = write(out, buf, (size_t)r) == r;
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    if (!ok) { LOG("[vk] driver %s: copying it to %s failed\n", name, dst); unlink(dst); return NULL; }
    LOG("[vk] driver %s: %s copied into internal storage (%lld bytes)\n", name, lib, (long long)ss.st_size);
    return dir;
}

static void open_loader(void) {
    void *lib = NULL;
    const char *env = getenv("LARECOMP_VK_DRIVER"), *drv = NULL;
    if (env && *env) drv = strcmp(env, "system") ? env : NULL;  // (a test run's choice: no trial, the setting untouched)
    else if (g_settings.vk_driver_trial[0]) {
        snprintf(g_failed, sizeof g_failed, "%s", g_settings.vk_driver_trial);
        LOG("[vk] driver %s: the last start didn't get the GPU up on it: the system's driver again\n", g_failed);
        g_settings.vk_driver[0] = g_settings.vk_driver_trial[0] = 0;
        settings_save();
    } else if (g_settings.vk_driver[0]) {
        drv = g_settings.vk_driver;
        snprintf(g_settings.vk_driver_trial, sizeof g_settings.vk_driver_trial, "%s", drv);
        settings_save();
        g_trial = true;
    }
    if (drv) {
        char dir[1100], hooks[1024], so[256];
        const char *ddir = driver_copy(drv, dir, sizeof dir, so, sizeof so);
        if (ddir && plat_exe_path(hooks, sizeof hooks) && strrchr(hooks, '/')) {
            strrchr(hooks, '/')[1] = 0;  // (libmain.so's folder: the APK's extracted libraries, adrenotools' hooks too)
            lib = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hooks, ddir, so,
                                             NULL, NULL);
        }
        if (lib) {
            snprintf(g_loaded, sizeof g_loaded, "%s", drv);
            LOG("[vk] driver: %s (%s) through libadrenotools, not the system's\n", drv, so);
        } else {
            snprintf(g_failed, sizeof g_failed, "%s", drv);
            LOG("[vk] driver %s: libadrenotools could not load it: the system's driver instead\n", drv);
        }
    }
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    g_gipa = lib ? (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr") : NULL;
    if (!g_gipa) {
        fprintf(stderr, "no Vulkan loader: %s\n", dlerror());
        abort();
    }
}

PFN_vkGetInstanceProcAddr vk_shim_gipa(void) {
    pthread_once(&g_once, open_loader);
    return g_gipa;
}

PFN_vkVoidFunction vk_shim_resolve(const char *name) {
    PFN_vkVoidFunction f = vk_shim_gipa()(g_inst, name);
    if (!f) LOG("[vk] %s: not in the driver\n", name);
    return f;
}

void vk_shim_instance(VkInstance inst) { g_inst = inst; }

// (vulkan/device.c ui_window_init: the device and swapchain are up) the chosen driver passed its trial
void plat_vk_driver_up(void) {
    if (!g_trial) return;
    g_trial = false;
    g_settings.vk_driver_trial[0] = 0;
    settings_save();
}

const char *plat_vk_driver_loaded(void) { return g_loaded[0] ? g_loaded : NULL; }
const char *plat_vk_driver_failed(void) { return g_failed[0] ? g_failed : NULL; }
bool plat_vk_driver_trial(void) { return g_trial; }

static int name_cmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

int plat_vk_drivers(char names[][96], int max) {
    const char *root = getenv("LARECOMP_DATA_ROOT");
    char p[1024], dir[1024], lib[256];
    if (!root) return 0;
    snprintf(p, sizeof p, "%s/drivers", root);
    DIR *d = opendir(p);
    if (!d) return 0;
    int n = 0;
    for (struct dirent *e; n < max && (e = readdir(d));)
        if (strlen(e->d_name) < 96 && driver_lib(e->d_name, dir, sizeof dir, lib, sizeof lib))
            snprintf(names[n++], 96, "%s", e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, name_cmp);
    return n;
}

bool plat_vk_driver_remove(const char *name) {
    const char *root = getenv("LARECOMP_DATA_ROOT"), *base = SDL_GetAndroidInternalStoragePath();
    char p[1024];
    if (!root || !*name || strchr(name, '/') || name[0] == '.') return false;
    snprintf(p, sizeof p, "%s/drivers/%s", root, name);
    bool ok = is_dir(p) ? plat_remove_tree(p) : unlink(p) == 0;
    if (base) {  // (its internal copy too; the running process keeps the one it mapped)
        snprintf(p, sizeof p, "%s/gpu_drivers/%s", base, name);
        plat_remove_tree(p);
    }
    LOG("[vk] driver %s deleted%s\n", name, ok ? "" : " (failed)");
    return ok;
}

// ---- import: a picked file (a path, or the content:// URI Android's file picker gives - SDL_LoadFile reads both)

// an Android library for this CPU: ELF, 64-bit, AArch64
static bool elf_arm64(const unsigned char *b, size_t n) {
    return n > 64 && !memcmp(b, "\x7f" "ELF", 4) && b[4] == 2 && (b[18] | b[19] << 8) == 183;
}
static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// A zip's entry by name (its last path part, any case), inflated; NULL if absent or unreadable. so_any: the first .so
// instead (a package without meta.json). Driver zips are a few MB: the whole archive is in memory already.
static unsigned char *zip_get(const unsigned char *z, size_t zn, const char *want, bool so_any, size_t *outn,
                              char *got, size_t gn) {
    size_t eocd = (size_t)-1, stop = zn > 65557 ? zn - 65557 : 0;  // (the end record: last, after a comment of < 64 KB)
    for (size_t i = zn >= 22 ? zn - 21 : 0; i-- > stop;)
        if (rd32(z + i) == 0x06054b50) { eocd = i; break; }
    if (eocd == (size_t)-1) return NULL;
    size_t cd = rd32(z + eocd + 16), count = rd16(z + eocd + 10);
    for (size_t k = 0; k < count && cd + 46 <= zn && rd32(z + cd) == 0x02014b50; k++) {
        const unsigned char *h = z + cd;
        unsigned method = rd16(h + 10), nl = rd16(h + 28), el = rd16(h + 30), cl = rd16(h + 32);
        size_t csz = rd32(h + 20), usz = rd32(h + 24), lo = rd32(h + 42);
        if (cd + 46 + nl > zn) return NULL;
        char name[512];
        snprintf(name, sizeof name, "%.*s", (int)nl, (const char *)h + 46);
        cd += 46 + nl + el + cl;
        const char *leaf = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
        if (!*leaf || !(so_any ? ends_with_so(leaf) : !strcasecmp(leaf, want))) continue;
        if (lo + 30 > zn || rd32(z + lo) != 0x04034b50) return NULL;
        size_t data = lo + 30 + rd16(z + lo + 26) + rd16(z + lo + 28);
        if (data + csz > zn || usz > (256u << 20)) return NULL;
        unsigned char *out = malloc(usz + 1);
        if (!out) return NULL;
        bool ok = false;
        if (method == 0 && csz == usz) memcpy(out, z + data, usz), ok = true;
        else if (method == 8) {
            z_stream s = {0};
            s.next_in = (Bytef *)(z + data), s.avail_in = (uInt)csz;
            s.next_out = out, s.avail_out = (uInt)usz;
            ok = inflateInit2(&s, -MAX_WBITS) == Z_OK && inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == usz;
            inflateEnd(&s);
        }
        if (!ok) { free(out); return NULL; }
        out[usz] = 0;
        *outn = usz;
        if (got) snprintf(got, gn, "%s", leaf);
        return out;
    }
    return NULL;
}

static bool write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

// the picked file's own name, from a path or a content:// URI ("…/document/primary%3ADownload%2FTurnip.zip"); "" when
// the URI doesn't carry one (the Downloads provider's "msf:1234")
static void picked_name(const char *src, char *out, size_t n) {
    char dec[1024];
    size_t k = 0;
    for (const char *p = src; *p && k + 1 < sizeof dec; p++) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char h[3] = {p[1], p[2], 0};
            dec[k++] = (char)strtol(h, NULL, 16);
            p += 2;
        } else dec[k++] = *p;
    }
    dec[k] = 0;
    const char *leaf = dec;
    for (const char *p = dec; *p; p++) if (*p == '/' || *p == ':') leaf = p + 1;
    snprintf(out, n, "%s", leaf);
    char *dot = strrchr(out, '.');
    if (dot && (!strcasecmp(dot, ".zip") || !strcasecmp(dot, ".so"))) *dot = 0;
    if (!strncmp(out, "msf", 3) || strspn(out, "0123456789") == strlen(out)) out[0] = 0;
}

bool plat_vk_driver_import(const char *src, char *msg, size_t mn) {
    const char *root = getenv("LARECOMP_DATA_ROOT");
    size_t n = 0;
    unsigned char *f = root ? SDL_LoadFile(src, &n) : NULL;
    if (!f) {
        snprintf(msg, mn, UI_TR("can't read the file (%s)", "não deu para ler o arquivo (%s)"), SDL_GetError());
        return false;
    }
    unsigned char *so = NULL, *meta = NULL;
    size_t son = 0, metan = 0;
    char name[96] = "", lib[256] = "", fname[256];
    picked_name(src, fname, sizeof fname);
    bool ok = false;
    if (elf_arm64(f, n)) {  // a bare library: named after the file
        so = f, son = n;
        safe_name(fname[0] ? fname : "driver", name, sizeof name);
        snprintf(lib, sizeof lib, "%s.so", name);
    } else if (n > 4 && rd32(f) == 0x04034b50) {
        meta = zip_get(f, n, "meta.json", false, &metan, NULL, 0);
        char want[256] = "", title[96] = "";
        if (meta) {
            json_str((const char *)meta, "libraryName", want, sizeof want);
            json_str((const char *)meta, "name", title, sizeof title);
        }
        so = want[0] ? zip_get(f, n, want, false, &son, lib, sizeof lib) : zip_get(f, n, NULL, true, &son, lib, sizeof lib);
        if (!so) { snprintf(msg, mn, "%s", UI_TR("no driver library (.so) in this zip", "nenhuma biblioteca de driver (.so) neste zip")); goto done; }
        safe_name(title[0] ? title : fname[0] ? fname : "driver", name, sizeof name);
    } else {
        snprintf(msg, mn, "%s", UI_TR("not a driver: a .zip package or an Android .so is expected",
                                       "não é um driver: precisa ser um pacote .zip ou um .so de Android"));
        goto done;
    }
    if (!elf_arm64(so, son)) { snprintf(msg, mn, UI_TR("%s isn't a library for this phone (64-bit ARM)", "%s não é uma biblioteca para este celular (ARM 64 bits)"), lib); goto done; }
    {
        char dir[1024], p[1400];
        snprintf(dir, sizeof dir, "%s/drivers", root);
        mkdir(dir, 0755);
        snprintf(dir, sizeof dir, "%s/drivers/%s", root, name);
        plat_remove_tree(dir);  // (the same driver again: replaced)
        mkdir(dir, 0755);
        snprintf(p, sizeof p, "%s/%s", dir, lib);
        ok = write_file(p, so, son);
        snprintf(p, sizeof p, "%s/meta.json", dir);
        if (ok && meta) ok = write_file(p, meta, metan);
        else if (ok) {  // (a bare .so: a meta.json of our own, so every package has one)
            char js[512];
            int k = snprintf(js, sizeof js, "{\"schemaVersion\": 1, \"name\": \"%s\", \"libraryName\": \"%s\"}\n", name, lib);
            ok = write_file(p, js, (size_t)k);
        }
        if (!ok) { plat_remove_tree(dir); snprintf(msg, mn, UI_TR("couldn't write drivers/%s (storage full?)", "não deu para gravar drivers/%s (armazenamento cheio?)"), name); goto done; }
        // (a stale internal copy of an older build of the same name: copied again at the next start)
        const char *base = SDL_GetAndroidInternalStoragePath();
        if (base) snprintf(p, sizeof p, "%s/gpu_drivers/%s", base, name), plat_remove_tree(p);
        snprintf(msg, mn, "%s", name);
        LOG("[vk] driver imported: %s (%s, %zu bytes)\n", name, lib, son);
    }
done:
    if (!ok) LOG("[vk] driver import from %s: %s\n", src, msg);
    if (so != f) free(so);
    free(meta);
    SDL_free(f);
    return ok;
}
