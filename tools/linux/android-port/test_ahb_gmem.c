// Can the guest memory window be memory the GPU sees, on a phone whose driver has no VK_EXT_external_memory_host?
// (Adreno: no host-pointer import, so shader global memory - every storage buffer on the Switch's GPU - read zero.)
// The plan this checks: the window's pages are an AHardwareBuffer (a BLOB: plain bytes), its dma-buf mapped by the CPU
// at a fixed address (where the guest window is), and the same AHardwareBuffer imported into Vulkan as a buffer
// (VK_ANDROID_external_memory_android_hardware_buffer). Steps, each printed:
//   1. allocate BLOBs of growing size (the window is ~3.2 GB)
//   2. get its file descriptors (AHardwareBuffer_sendHandleToUnixSocket: the NDK's way out of a native handle)
//   3. mmap the dma-buf with MAP_FIXED at a reserved address
//   4. import it into Vulkan, and check both directions: CPU writes -> GPU copy reads them; GPU fill -> CPU sees it
// Build: build_test_ahb.sh. Run: adb push it to /data/local/tmp and run it there.
#include <android/hardware_buffer.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// The AHardwareBuffer's file descriptors, received through a socket pair (the number found: *n)
static int ahb_fds(AHardwareBuffer *b, int *fds, int max) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) return -1;
    if (AHardwareBuffer_sendHandleToUnixSocket(b, sv[0])) { close(sv[0]); close(sv[1]); return -1; }
    char data[4096];
    char ctl[CMSG_SPACE(sizeof(int) * 32)];
    struct iovec iov = {data, sizeof data};
    struct msghdr m = {.msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof ctl};
    ssize_t r = recvmsg(sv[1], &m, 0);
    close(sv[0]);
    close(sv[1]);
    if (r < 0) return -1;
    int n = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int k = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < k && n < max; i++) fds[n++] = ((int *)CMSG_DATA(c))[i];
        }
    return n;
}

static VkInstance inst;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;

static int vk_init(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    if (vkCreateInstance(&ici, NULL, &inst)) return -1;
    uint32_t n = 1;
    vkEnumeratePhysicalDevices(inst, &n, &phys);
    if (!n) return -1;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, NULL);
    VkQueueFamilyProperties qp[8];
    if (nq > 8) nq = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qp);
    for (qfam = 0; qfam < nq && !(qp[qfam].queueFlags & VK_QUEUE_GRAPHICS_BIT); qfam++) {}
    float pr = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qfam,
                                   .queueCount = 1, .pQueuePriorities = &pr};
    const char *ext[] = {"VK_ANDROID_external_memory_android_hardware_buffer", "VK_EXT_queue_family_foreign"};
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .bufferDeviceAddress = VK_TRUE};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f12, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .enabledExtensionCount = 2, .ppEnabledExtensionNames = ext};
    VkResult r = vkCreateDevice(phys, &dci, NULL, &dev);
    if (r) { printf("vkCreateDevice: %d\n", r); return -1; }
    vkGetDeviceQueue(dev, qfam, 0, &queue);
    return 0;
}

static int32_t find_type(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int32_t)i;
    return -1;
}

// A host-visible buffer to read the GPU's copy back
static VkBuffer rb_buf;
static VkDeviceMemory rb_mem;
static uint8_t *rb_ptr;
static int readback_init(uint64_t size) {
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (vkCreateBuffer(dev, &bi, NULL, &rb_buf)) return -1;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(dev, rb_buf, &rq);
    int32_t t = find_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = (uint32_t)t};
    if (t < 0 || vkAllocateMemory(dev, &ai, NULL, &rb_mem) || vkBindBufferMemory(dev, rb_buf, rb_mem, 0)) return -1;
    return vkMapMemory(dev, rb_mem, 0, VK_WHOLE_SIZE, 0, (void **)&rb_ptr) ? -1 : 0;
}

// One command buffer: copy `n` bytes at each of `offs` from the window buffer into the readback buffer, then fill
// [fill_off, fill_off + 4096) of the window with `fill`
static int gpu_round(VkBuffer win, const uint64_t *offs, int k, uint64_t n, uint64_t fill_off, uint32_t fill) {
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qfam};
    VkCommandPool pool;
    vkCreateCommandPool(dev, &pci, NULL, &pool);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);
    // (host writes made before the submission are visible to it: vkQueueSubmit is a host-to-device memory dependency)
    for (int i = 0; i < k; i++) {
        VkBufferCopy c = {offs[i], (uint64_t)i * n, n};
        vkCmdCopyBuffer(cb, win, rb_buf, 1, &c);
    }
    // (vkCmdFillBuffer wrote nothing into the imported buffer on the Qualcomm driver: an update instead, a transfer write too)
    static uint32_t words[1024];
    for (int i = 0; i < 1024; i++) words[i] = fill;
    if (getenv("FILL")) vkCmdFillBuffer(cb, win, fill_off, 4096, fill);
    else vkCmdUpdateBuffer(cb, win, fill_off, sizeof words, words);
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

static void try_size(uint64_t size, uint64_t at) {
    printf("\n=== %llu MB at %llx ===\n", (unsigned long long)(size >> 20), (unsigned long long)at);
    AHardwareBuffer_Desc d = {.width = (uint32_t)size, .height = 1, .layers = 1, .format = AHARDWAREBUFFER_FORMAT_BLOB,
                              .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                                       AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER};
    if (size > 0xFFFFFFFFull) { printf("1. too big for one BLOB (width is 32 bits)\n"); return; }
    AHardwareBuffer *b = NULL;
    double t0 = now_ms();
    int e = AHardwareBuffer_allocate(&d, &b);
    printf("1. allocate: %s (%d) in %.0f ms\n", e ? "FAILED" : "ok", e, now_ms() - t0);
    if (e) return;
    int fds[32];
    int nfd = ahb_fds(b, fds, 32);
    printf("2. file descriptors: %d\n", nfd);
    int dmafd = -1;
    for (int i = 0; i < nfd; i++) {
        char p[64], l[256] = "";
        snprintf(p, sizeof p, "/proc/self/fd/%d", fds[i]);
        ssize_t k = readlink(p, l, sizeof l - 1);
        if (k > 0) l[k] = 0;
        off_t end = lseek(fds[i], 0, SEEK_END);
        printf("   fd %d -> %s, size %lld\n", fds[i], l, (long long)end);
        if (dmafd < 0 && (uint64_t)end >= size) dmafd = fds[i];
    }
    if (dmafd < 0) { printf("3. no fd as big as the buffer\n"); AHardwareBuffer_release(b); return; }
    // reserve, as the runtime does (MAP_NORESERVE anonymous), then replace it
    void *res = mmap((void *)(uintptr_t)at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (res != (void *)(uintptr_t)at) { printf("3. reservation at %llx failed: %s\n", (unsigned long long)at, strerror(errno)); AHardwareBuffer_release(b); return; }
    t0 = now_ms();
    void *m = mmap((void *)(uintptr_t)at, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, dmafd, 0);
    printf("3. mmap MAP_FIXED: %s in %.0f ms\n", m == (void *)(uintptr_t)at ? "ok" : strerror(errno), now_ms() - t0);
    if (m != (void *)(uintptr_t)at) { munmap(res, size); AHardwareBuffer_release(b); return; }
    volatile uint32_t *w = (volatile uint32_t *)m;
    int zero_ok = w[0] == 0 && w[size / 8] == 0 && w[size / 4 - 1] == 0;
    printf("   zeroed at start: %s\n", zero_ok ? "yes" : "NO");
    // the CPU writes, before the GPU reads
    uint64_t offs[4] = {0, size / 3 & ~4095ull, size / 2 & ~4095ull, (size - 4096)};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 1024; j++) w[offs[i] / 4 + j] = 0xC0DE0000u + (uint32_t)(i * 1024 + j);

    // Vulkan import
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID props =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(dev, "vkGetAndroidHardwareBufferPropertiesANDROID");
    VkAndroidHardwareBufferPropertiesANDROID ap = {.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    VkResult r = props ? props(dev, b, &ap) : VK_ERROR_EXTENSION_NOT_PRESENT;
    printf("4. properties: %d, allocation %llu MB, memory types %#x\n", r, (unsigned long long)(ap.allocationSize >> 20), ap.memoryTypeBits);
    if (r) goto out;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if (ap.memoryTypeBits >> i & 1) printf("   type %u flags %#x (heap %u)\n", i, mp.memoryTypes[i].propertyFlags, mp.memoryTypes[i].heapIndex);
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &eb, .size = size,
                              .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    VkBuffer buf;
    r = vkCreateBuffer(dev, &bci, NULL, &buf);
    printf("   buffer: %d\n", r);
    if (r) goto out;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(dev, buf, &rq);
    printf("   buffer needs %llu MB, types %#x\n", (unsigned long long)(rq.size >> 20), rq.memoryTypeBits);
    VkImportAndroidHardwareBufferInfoANDROID imp = {.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, .buffer = b};
    VkMemoryDedicatedAllocateInfo ded = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &imp, .buffer = buf};
    VkMemoryAllocateFlagsInfo fl = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = &ded, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &fl, .allocationSize = ap.allocationSize,
                               .memoryTypeIndex = 0};
    {   // host-visible coherent first (GPU writes then reach the CPU's view), else whatever the import allows
        int32_t t = find_type(ap.memoryTypeBits & rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ai.memoryTypeIndex = t >= 0 ? (uint32_t)t : (uint32_t)__builtin_ctz(ap.memoryTypeBits & rq.memoryTypeBits);
        printf("   memory type %u\n", ai.memoryTypeIndex);
    }
    VkDeviceMemory mem;
    t0 = now_ms();
    r = vkAllocateMemory(dev, &ai, NULL, &mem);
    printf("   import: %d in %.0f ms\n", r, now_ms() - t0);
    if (r) { vkDestroyBuffer(dev, buf, NULL); goto out; }
    r = vkBindBufferMemory(dev, buf, mem, 0);
    printf("   bind: %d\n", r);
    if (!r) {
        uint64_t fill_off = size / 4 & ~4095ull;
        t0 = now_ms();
        r = gpu_round(buf, offs, 4, 4096, fill_off, 0xFEEDF00Du);
        printf("   GPU round: %d in %.1f ms\n", r, now_ms() - t0);
        int bad = 0;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 1024; j++)
                if (((uint32_t *)rb_ptr)[i * 1024 + j] != 0xC0DE0000u + (uint32_t)(i * 1024 + j)) bad++;
        printf("   CPU -> GPU: %s (%d words of 4096 wrong)\n", bad ? "WRONG" : "ok", bad);
        bad = 0;
        for (int j = 0; j < 1024; j++) if (w[fill_off / 4 + j] != 0xFEEDF00Du) bad++;
        printf("   GPU -> CPU: %s (%d words of 1024 wrong; first word %#x)\n", bad ? "WRONG" : "ok", bad, w[fill_off / 4]);
        if (bad) {  // where did the fill go: the GPU's own view, Vulkan's mapping, and our mapping after a cache sync
            uint64_t o[1] = {fill_off};
            gpu_round(buf, o, 1, 4096, size - 8192, 0);  // (the fill region copied back by the GPU; a harmless fill elsewhere)
            printf("   the GPU reads back %#x there\n", ((uint32_t *)rb_ptr)[0]);
            void *vp = NULL;
            VkResult mr = vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &vp);
            if (!mr && vp) { printf("   Vulkan's own mapping reads %#x\n", ((volatile uint32_t *)vp)[fill_off / 4]); vkUnmapMemory(dev, mem); }
            else printf("   vkMapMemory: %d\n", mr);
            struct { uint64_t flags; } sync = {(1 << 0) | (0 << 2)};  // DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START
            int se = ioctl(dmafd, _IOW('b', 0, uint64_t), &sync);
            printf("   after DMA_BUF_IOCTL_SYNC (%d): our mapping reads %#x\n", se, w[fill_off / 4]);
            sync.flags = (1 << 0) | (1 << 2);  // ... | DMA_BUF_SYNC_END
            ioctl(dmafd, _IOW('b', 0, uint64_t), &sync);
        }
        // and again, the same pages rewritten (a stale CPU cache line would show here)
        for (int i = 0; i < 4; i++) for (int j = 0; j < 1024; j++) w[offs[i] / 4 + j] = 0xAB000000u + (uint32_t)j;
        r = gpu_round(buf, offs, 4, 4096, fill_off, 0x11223344u);
        bad = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 1024; j++) if (((uint32_t *)rb_ptr)[i * 1024 + j] != 0xAB000000u + (uint32_t)j) bad++;
        int bad2 = 0;
        for (int j = 0; j < 1024; j++) if (w[fill_off / 4 + j] != 0x11223344u) bad2++;
        printf("   second round: CPU -> GPU %s, GPU -> CPU %s\n", bad ? "WRONG" : "ok", bad2 ? "WRONG" : "ok");
        if (!bad && !bad2) printf("   WORKS at %llu MB\n", (unsigned long long)(size >> 20));
    }
    vkDestroyBuffer(dev, buf, NULL);
    vkFreeMemory(dev, mem, NULL);
out:
    munmap(m, size);
    for (int i = 0; i < nfd; i++) close(fds[i]);
    AHardwareBuffer_release(b);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (vk_init()) { printf("Vulkan init failed\n"); return 1; }
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(phys, &pp);
    printf("GPU: %s\n", pp.deviceName);
    if (readback_init(4 * 4096)) { printf("readback buffer failed\n"); return 1; }
    // the window's own size (Z-A: 0xc9500000) last; the runtime's guest window sits at 0x20_0000_0000
    // sizes in MB on the command line (default: 256 and the window's own size, Z-A's 0xc9500000)
    if (argc > 1) for (int i = 1; i < argc; i++) try_size(strtoull(argv[i], NULL, 0) << 20, 0x2100000000ull);
    else { try_size(256ull << 20, 0x2100000000ull); try_size(0xc9500000ull, 0x2100000000ull); }
    return 0;
}
