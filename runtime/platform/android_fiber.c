// See runtime/platform/android_fiber.h: the host context switch fiber.c needs, which bionic does not provide
// on arm64. Only co_ctx_swap is missing an implementation; the other two are already what they will be.
#include "android_fiber.h"

#include <string.h>

#include "../hle.h"

void co_ctx_get(CoCtx *c) {
    // Nothing to capture: a context is only ever read after co_ctx_make has filled it, or written by a swap.
    // (On POSIX getcontext exists because makecontext requires an initialised context; here it does not.)
    memset(c, 0, sizeof *c);
}

void co_ctx_make(CoCtx *c, void (*entry)(void)) {
    c->entry = entry;
    // The stack grows down from its top, 16-byte aligned as AAPCS64 requires at a function boundary.
    uintptr_t top = (uintptr_t)c->uc_stack.ss_sp + c->uc_stack.ss_size;
    c->sp = (uint64_t)(top & ~(uintptr_t)15);
}

void co_ctx_swap(CoCtx *save, CoCtx *to) {
    (void)save, (void)to;
    // Reached only by a game that actually uses nn::os fibers - Pokémon Z-A's job system does, Link's Awakening
    // does not. Writing the switch is the next step; failing loudly beats corrupting two host stacks.
    FATAL("[fiber] host fiber switching is not implemented on Android yet (runtime/platform/android_fiber.c).\n"
          "This game needs it; Link's Awakening does not.\n");
}
