// What a custom Turnip build (Mesa's Adreno driver, as emulators load it) gives the runtime on this phone, loaded
// straight from a file instead of the system's driver: the extensions that matter here, and whether its host-pointer
// import (VK_EXT_external_memory_host - the system driver has none, so shader global memory read zero) can take the
// guest window, with both directions of a CPU / GPU round trip checked.
// Usage: test_turnip <driver.so> [MB ...]   (default sizes: 256, then the Z-A window's 0xc9500000)
// The driver is an Android Vulkan HAL module (it exports HMI): opened as the system loader would, then everything is
// called through the function pointers it hands out - no libvulkan, no loader.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// hardware/hardware.h + hardware/hwvulkan.h (LP64 layout)
struct hw_module_t;
struct hw_device_t {
    uint32_t tag, version;
    struct hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *);
};
struct hw_module_methods_t {
    int (*open)(const struct hw_module_t *, const char *id, struct hw_device_t **);
};
struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[32 - 7];
};
typedef struct {
    struct hw_device_t common;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
} hwvulkan_device_t;

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static PFN_vkGetInstanceProcAddr gipa;
static VkInstance inst;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
#define IP(name) PFN_##name name = (PFN_##name)gipa(inst, #name)
static PFN_vkGetDeviceProcAddr gdpa;
#define DP(name) PFN_##name name = (PFN_##name)gdpa(dev, #name)

static bool has_ext(VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return true;
    return false;
}

static int32_t find_type(uint32_t bits, VkMemoryPropertyFlags want) {
    IP(vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int32_t)i;
    return -1;
}

static VkBuffer rb_buf;
static uint8_t *rb_ptr;

static VkResult make_buffer(uint64_t size, VkBufferUsageFlags usage, const void *next, VkBuffer *b) {
    DP(vkCreateBuffer);
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = next, .size = size, .usage = usage};
    return vkCreateBuffer(dev, &bi, NULL, b);
}

static int readback_init(uint64_t size) {
    DP(vkGetBufferMemoryRequirements); DP(vkAllocateMemory); DP(vkBindBufferMemory); DP(vkMapMemory);
    if (make_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, NULL, &rb_buf)) return -1;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(dev, rb_buf, &rq);
    int32_t t = find_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = (uint32_t)t};
    VkDeviceMemory m;
    if (t < 0 || vkAllocateMemory(dev, &ai, NULL, &m) || vkBindBufferMemory(dev, rb_buf, m, 0)) return -1;
    return vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, (void **)&rb_ptr) ? -1 : 0;
}

// copy 4 KB at each of offs[] into the readback buffer, then fill 4 KB at fill_off with `fill`; host-visible after
static VkResult gpu_round(VkBuffer win, const uint64_t *offs, int k, uint64_t fill_off, uint32_t fill) {
    DP(vkCreateCommandPool); DP(vkAllocateCommandBuffers); DP(vkBeginCommandBuffer); DP(vkCmdCopyBuffer);
    DP(vkCmdFillBuffer); DP(vkCmdPipelineBarrier); DP(vkEndCommandBuffer); DP(vkCreateFence); DP(vkQueueSubmit);
    DP(vkWaitForFences); DP(vkDestroyFence); DP(vkDestroyCommandPool);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qfam};
    VkCommandPool pool;
    vkCreateCommandPool(dev, &pci, NULL, &pool);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);
    *(void **)cb = *(void **)dev;  // (a dispatchable object's loader slot: no loader here, any non-null value will do)
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);
    for (int i = 0; i < k; i++) {
        VkBufferCopy c = {offs[i], (uint64_t)i * 4096, 4096};
        vkCmdCopyBuffer(cb, win, rb_buf, 1, &c);
    }
    vkCmdFillBuffer(cb, win, fill_off, 4096, fill);
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    vkEndCommandBuffer(cb);
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence f;
    vkCreateFence(dev, &fci, NULL, &f);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    VkResult r = vkQueueSubmit(queue, 1, &si, f);
    if (!r) r = vkWaitForFences(dev, 1, &f, VK_TRUE, 10000000000ull);
    vkDestroyFence(dev, f, NULL);
    vkDestroyCommandPool(dev, pool, NULL);
    return r;
}

// Both directions over [p, p + size), the window buffer `buf` laid over it
static void round_trip(volatile uint32_t *w, uint64_t size, VkBuffer buf) {
    uint64_t offs[4] = {0, size / 3 & ~4095ull, size / 2 & ~4095ull, size - 4096}, fill_off = size / 4 & ~4095ull;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t tag = pass ? 0xAB000000u : 0xC0DE0000u, fill = pass ? 0x11223344u : 0xFEEDF00Du;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 1024; j++) w[offs[i] / 4 + j] = tag + (uint32_t)(i * 1024 + j);
        double t0 = now_ms();
        VkResult r = gpu_round(buf, offs, 4, fill_off, fill);
        int bad = 0, bad2 = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 1024; j++)
            if (((uint32_t *)rb_ptr)[i * 1024 + j] != tag + (uint32_t)(i * 1024 + j)) bad++;
        for (int j = 0; j < 1024; j++) if (w[fill_off / 4 + j] != fill) bad2++;
        printf("   pass %d (%d, %.1f ms): CPU -> GPU %s (%d wrong), GPU -> CPU %s (%d wrong)\n", pass, r, now_ms() - t0,
               bad ? "WRONG" : "ok", bad, bad2 ? "WRONG" : "ok", bad2);
    }
}

// The runtime's way: anonymous memory at a fixed address, imported by host pointer
static void try_host_import(uint64_t size, uint64_t at) {
    DP(vkGetMemoryHostPointerPropertiesEXT); DP(vkGetBufferMemoryRequirements); DP(vkAllocateMemory);
    DP(vkBindBufferMemory); DP(vkDestroyBuffer); DP(vkFreeMemory);
    printf("\n=== host-pointer import: %llu MB at %llx ===\n", (unsigned long long)(size >> 20), (unsigned long long)at);
    void *p = mmap((void *)(uintptr_t)at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)(uintptr_t)at) { printf("   reservation failed: %s\n", strerror(errno)); return; }
    VkMemoryHostPointerPropertiesEXT mp = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    VkResult r = vkGetMemoryHostPointerPropertiesEXT
                     ? vkGetMemoryHostPointerPropertiesEXT(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, p, &mp)
                     : VK_ERROR_EXTENSION_NOT_PRESENT;
    printf("   pointer properties: %d, types %#x\n", r, mp.memoryTypeBits);
    if (r || !mp.memoryTypeBits) { munmap(p, size); return; }
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkBuffer buf;
    r = make_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &eb, &buf);
    if (r) { printf("   buffer: %d\n", r); munmap(p, size); return; }
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(dev, buf, &rq);
    VkImportMemoryHostPointerInfoEXT imp = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, .pHostPointer = p};
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &imp, .allocationSize = size,
                               .memoryTypeIndex = (uint32_t)__builtin_ctz(mp.memoryTypeBits & rq.memoryTypeBits)};
    VkDeviceMemory mem;
    double t0 = now_ms();
    r = vkAllocateMemory(dev, &ai, NULL, &mem);
    printf("   import: %d in %.0f ms (memory type %u)\n", r, now_ms() - t0, ai.memoryTypeIndex);
    if (!r && !(r = vkBindBufferMemory(dev, buf, mem, 0))) round_trip((volatile uint32_t *)p, size, buf);
    else if (r) printf("   bind / import failed: %d\n", r);
    vkDestroyBuffer(dev, buf, NULL);
    if (!r) vkFreeMemory(dev, mem, NULL);
    munmap(p, size);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { printf("usage: %s <driver.so> [MB ...]\n", argv[0]); return 1; }
    void *so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!so) { printf("dlopen: %s\n", dlerror()); return 1; }
    struct hw_module_t *hmi = (struct hw_module_t *)dlsym(so, "HMI");
    if (!hmi) { printf("no HMI\n"); return 1; }
    printf("driver module: %s by %s\n", hmi->name, hmi->author);
    hwvulkan_device_t *hd = NULL;
    int e = hmi->methods->open(hmi, "vk0", (struct hw_device_t **)&hd);
    if (e || !hd) { printf("open: %d\n", e); return 1; }
    gipa = hd->GetInstanceProcAddr;

    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkResult r = hd->CreateInstance(&ici, NULL, &inst);
    if (r) { printf("vkCreateInstance: %d\n", r); return 1; }
    IP(vkEnumeratePhysicalDevices); IP(vkGetPhysicalDeviceProperties); IP(vkEnumerateDeviceExtensionProperties);
    IP(vkGetPhysicalDeviceQueueFamilyProperties); IP(vkCreateDevice); IP(vkGetPhysicalDeviceFeatures);
    uint32_t n = 1;
    r = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if (r || !n) { printf("no physical device (%d)\n", r); return 1; }
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(phys, &pp);
    printf("GPU: %s, Vulkan %u.%u.%u, driver %#x\n", pp.deviceName, VK_API_VERSION_MAJOR(pp.apiVersion),
           VK_API_VERSION_MINOR(pp.apiVersion), VK_API_VERSION_PATCH(pp.apiVersion), pp.driverVersion);
    uint32_t ne = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &ne, NULL);
    VkExtensionProperties *ex = calloc(ne, sizeof *ex);
    vkEnumerateDeviceExtensionProperties(phys, NULL, &ne, ex);
    printf("%u device extensions; the ones the runtime looks for:\n", ne);
    const char *want[] = {"VK_EXT_external_memory_host", "VK_EXT_map_memory_placed", "VK_EXT_external_memory_dma_buf",
                          "VK_KHR_external_memory_fd", "VK_EXT_transform_feedback", "VK_EXT_vertex_input_dynamic_state",
                          "VK_EXT_extended_dynamic_state3", "VK_EXT_graphics_pipeline_library", "VK_KHR_present_wait",
                          "VK_EXT_robustness2", "VK_EXT_custom_border_color", "VK_EXT_shader_stencil_export",
                          "VK_EXT_depth_clip_control", "VK_KHR_swapchain", "VK_ANDROID_external_memory_android_hardware_buffer"};
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) printf("  %-52s %s\n", want[i], has_ext(ex, ne, want[i]) ? "yes" : "-");
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(phys, &f);
    printf("textureCompressionBC %d, shaderFloat64 %d, shaderInt64 %d, vertexPipelineStoresAndAtomics %d, geometryShader %d\n",
           f.textureCompressionBC, f.shaderFloat64, f.shaderInt64, f.vertexPipelineStoresAndAtomics, f.geometryShader);
    printf("maxStorageBufferRange %u MB\n", pp.limits.maxStorageBufferRange >> 20);

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, NULL);
    VkQueueFamilyProperties qp[8];
    if (nq > 8) nq = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qp);
    for (qfam = 0; qfam < nq && !(qp[qfam].queueFlags & VK_QUEUE_GRAPHICS_BIT); qfam++) {}
    float pr = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qfam,
                                   .queueCount = 1, .pQueuePriorities = &pr};
    const char *dext[4];
    uint32_t nd = 0;
    bool host = has_ext(ex, ne, "VK_EXT_external_memory_host");
    if (host) dext[nd++] = "VK_EXT_external_memory_host";
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                              .enabledExtensionCount = nd, .ppEnabledExtensionNames = dext};
    r = vkCreateDevice(phys, &dci, NULL, &dev);
    if (r) { printf("vkCreateDevice: %d\n", r); return 1; }
    gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");
    DP(vkGetDeviceQueue);
    vkGetDeviceQueue(dev, qfam, 0, &queue);
    *(void **)queue = *(void **)dev;  // (loader slot, as for the command buffer)
    if (readback_init(4 * 4096)) { printf("readback buffer failed\n"); return 1; }
    if (!host) { printf("\nno VK_EXT_external_memory_host: nothing more to test\n"); return 0; }
    if (argc > 2) for (int i = 2; i < argc; i++) try_host_import(strtoull(argv[i], NULL, 0) << 20, 0x2100000000ull);
    else { try_host_import(256ull << 20, 0x2100000000ull); try_host_import(0xc9500000ull, 0x2100000000ull); }
    printf("\ndone\n");
    return 0;
}
