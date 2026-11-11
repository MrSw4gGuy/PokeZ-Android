// Android: which Vulkan driver the runtime talks to. libmain.so carries every Vulkan command itself (tools/gen_vk_shim.py,
// built into $(B)/inc/vk_shim.c) and links no libvulkan.so; each command is looked up here, once, through the
// vkGetInstanceProcAddr of the loader opened below.
//   - Default: the system's loader and Qualcomm's driver, as before (the same libvulkan.so a -lvulkan link gave).
//   - LARECOMP_VK_DRIVER=<file> (env.txt; e.g. a Turnip build, Mesa's open Adreno driver, as emulators load it): the
//     file from <data root>/drivers/ is copied into the app's internal storage (an app cannot load code from shared
//     storage) and libadrenotools (third_party/libadrenotools, BSD-2-Clause) opens a second copy of the system's loader
//     in a linker namespace of its own, whose driver is that file. The loader still makes the surface and the swapchain
//     over the driver's VK_ANDROID_native_buffer. Its hook libraries ship in the APK's lib folder (make_project.sh).
//     A driver that does not load leaves the system's, logged. Another port measured Turnip on an Adreno 830 at GPU
//     16-17 ms a frame against 22-27 ms on Qualcomm's driver in the same scene (nx2android's gfx_vk_core.c, TOTKDroid).
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../platform.h"
#include "../rt.h"

static PFN_vkGetInstanceProcAddr g_gipa;
static VkInstance g_inst;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

// <data root>/drivers/<name> -> <internal>/vkdriver/<name>, copied again only when the size differs. Returns the
// internal folder with its trailing slash (adrenotools joins folder and name as they are), or NULL.
static const char *driver_copy(const char *name, char *dir, size_t n) {
    const char *root = getenv("LARECOMP_DATA_ROOT"), *base = SDL_GetAndroidInternalStoragePath();
    if (!root || !base) return NULL;
    char src[1024], dst[1200];
    snprintf(src, sizeof src, "%s/drivers/%s", root, name);
    snprintf(dir, n, "%s/vkdriver/", base);
    mkdir(dir, 0755);
    snprintf(dst, sizeof dst, "%s%s", dir, name);
    struct stat ss, ds;
    if (stat(src, &ss) != 0) {
        if (stat(dst, &ds) == 0) return dir;  // (copied by an earlier start; the shared copy since removed)
        LOG("[vk] driver %s: no %s\n", name, src);
        return NULL;
    }
    if (stat(dst, &ds) == 0 && ds.st_size == ss.st_size) return dir;
    int in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    bool ok = in >= 0 && out >= 0;
    char buf[1 << 16];
    for (ssize_t r; ok && (r = read(in, buf, sizeof buf)) > 0;) ok = write(out, buf, (size_t)r) == r;
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    if (!ok) { LOG("[vk] driver %s: copying it to %s failed\n", name, dst); unlink(dst); return NULL; }
    LOG("[vk] driver %s: copied into internal storage (%lld bytes)\n", name, (long long)ss.st_size);
    return dir;
}

static void open_loader(void) {
    void *lib = NULL;
    const char *drv = getenv("LARECOMP_VK_DRIVER");
    if (drv && *drv && strcmp(drv, "system") != 0) {
        char dir[1100], hooks[1024];
        const char *ddir = driver_copy(drv, dir, sizeof dir);
        if (ddir && plat_exe_path(hooks, sizeof hooks) && strrchr(hooks, '/')) {
            strrchr(hooks, '/')[1] = 0;  // (libmain.so's folder: the APK's extracted libraries, adrenotools' hooks too)
            lib = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hooks, ddir, drv,
                                             NULL, NULL);
            if (lib) LOG("[vk] driver: %s through libadrenotools (not the system's)\n", drv);
            else LOG("[vk] driver %s: libadrenotools could not load it: the system's driver instead\n", drv);
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
