// runtime/platform/android_fiber.c on a real device, before a game depends on it (test_fiber_device.sh builds it
// with a stand-in hle.h and runs it over adb). A wrong register in a context switch shows up as random corruption far
// from its cause, so it is checked on its own: a million round trips with callee-saved integer and FP state live
// across every switch, a fresh context entering on its own stack, and a context suspended on one thread resumed on
// another - what Pokemon Legends Z-A's job system does with its fibers (runtime/hle/fiber.c).
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "android_fiber.h"

#define ROUNDS 1000000L

static CoCtx fib, main_ctx, thread_ctx;
static CoCtx *back;  // whoever resumed the fiber: it switches back there
static long fib_runs;
static double fib_f = 1.0;

static void fib_entry(void) {
    double f = 1.0;  // lives in a callee-saved FP register across co_ctx_swap
    long n = 0;
    for (;;) {
        n++;
        f = f * 1.0000001 + 0.5;
        fib_runs = n, fib_f = f;
        co_ctx_swap(&fib, back);
    }
}

static void *resume_elsewhere(void *arg) {
    back = &thread_ctx;
    co_ctx_swap(&thread_ctx, &fib);  // the fiber last ran on the main thread
    return arg;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void) {
    size_t sz = 1 << 20;
    void *stack = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) return 2;
    co_ctx_get(&fib);
    fib.uc_stack.ss_sp = stack, fib.uc_stack.ss_size = sz;
    co_ctx_make(&fib, fib_entry);

    // state of the main context the switches must leave as it was (kept in registers across the calls)
    long a = 7, b = 11;
    double x = 0.25, y = 3.5;
    back = &main_ctx;
    double t0 = now();
    for (long i = 0; i < ROUNDS; i++) {
        co_ctx_swap(&main_ctx, &fib);
        a += i & 3, b ^= i, x += 0.5, y *= 1.0000001;
    }
    double dt = now() - t0;

    long a2 = 7, b2 = 11;
    double x2 = 0.25, y2 = 3.5;
    for (long i = 0; i < ROUNDS; i++) a2 += i & 3, b2 ^= i, x2 += 0.5, y2 *= 1.0000001;
    int ok = 1;
    if (a != a2 || b != b2 || x != x2 || y != y2) { printf("FAIL: main context's registers changed\n"); ok = 0; }
    if (fib_runs != ROUNDS) { printf("FAIL: fiber ran %ld times, not %ld\n", fib_runs, ROUNDS); ok = 0; }
    double f2 = 1.0;
    for (long i = 0; i < ROUNDS; i++) f2 = f2 * 1.0000001 + 0.5;
    if (fib_f != f2) { printf("FAIL: the fiber's FP state was lost\n"); ok = 0; }

    pthread_t t;  // resumed on another thread, then back on this one
    pthread_create(&t, NULL, resume_elsewhere, NULL);
    pthread_join(t, NULL);
    if (fib_runs != ROUNDS + 1) { printf("FAIL: the fiber did not run on the other thread\n"); ok = 0; }
    back = &main_ctx;
    co_ctx_swap(&main_ctx, &fib);
    if (fib_runs != ROUNDS + 2) { printf("FAIL: the fiber did not come back to this thread\n"); ok = 0; }

    printf("%ld round trips, %.0f ns each; resumed on another thread and back\n", ROUNDS, dt / ROUNDS * 1e9);
    printf(ok ? "FIBER OK\n" : "FIBER FAILED\n");
    return ok ? 0 : 1;
}
