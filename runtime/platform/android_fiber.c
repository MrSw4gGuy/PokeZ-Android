// See runtime/platform/android_fiber.h: the host context switch fiber.c needs, which bionic does not provide on arm64.
//
// co_ctx_swap saves the AAPCS64 callee-saved state - x19-x28, fp, lr, sp, d8-d15 - into one context and loads the
// other's, then returns: into the other context's caller of co_ctx_swap, or, the first time, into co_ctx_start, which
// calls its entry. Nothing else needs saving: every other register is caller-saved, so the C code that called
// co_ctx_swap has given it up already. x18 is left alone: on Android it holds the thread's shadow call stack, and none
// of the frames a switch moves between (the runtime's and the recompiled game's) is instrumented to use it.
#include "android_fiber.h"

#include <string.h>

#include "../hle.h"

// the offsets the assembly below uses
_Static_assert(offsetof(CoCtx, x) == 0, "co_ctx_swap: x19 at 0");
_Static_assert(offsetof(CoCtx, sp) == 96, "co_ctx_swap: sp at 96");
_Static_assert(offsetof(CoCtx, d) == 104, "co_ctx_swap: d8 at 104");

void co_ctx_start(void);  // (assembly) where a fresh context's first switch returns to

// Reached only if a fiber's entry function returned, which neither of fiber.c's does (co_main and tramp_main loop):
// with ucontext and uc_link NULL the thread would have ended; here there is nowhere sensible to go.
__attribute__((used, noreturn)) void co_ctx_returned(void) {
    FATAL("[fiber] a fiber's entry function returned (runtime/platform/android_fiber.c)\n");
    __builtin_unreachable();
}

__asm__(".text\n"
        ".balign 16\n"
        ".global co_ctx_swap\n"
        ".type co_ctx_swap, %function\n"
        "co_ctx_swap:\n"  // x0: the context to save into, x1: the one to switch to
        "  stp x19, x20, [x0, #0]\n"
        "  stp x21, x22, [x0, #16]\n"
        "  stp x23, x24, [x0, #32]\n"
        "  stp x25, x26, [x0, #48]\n"
        "  stp x27, x28, [x0, #64]\n"
        "  stp x29, x30, [x0, #80]\n"
        "  mov x9, sp\n"
        "  str x9, [x0, #96]\n"
        "  stp d8, d9, [x0, #104]\n"
        "  stp d10, d11, [x0, #120]\n"
        "  stp d12, d13, [x0, #136]\n"
        "  stp d14, d15, [x0, #152]\n"
        "  ldp x19, x20, [x1, #0]\n"
        "  ldp x21, x22, [x1, #16]\n"
        "  ldp x23, x24, [x1, #32]\n"
        "  ldp x25, x26, [x1, #48]\n"
        "  ldp x27, x28, [x1, #64]\n"
        "  ldp x29, x30, [x1, #80]\n"
        "  ldr x9, [x1, #96]\n"
        "  mov sp, x9\n"
        "  ldp d8, d9, [x1, #104]\n"
        "  ldp d10, d11, [x1, #120]\n"
        "  ldp d12, d13, [x1, #136]\n"
        "  ldp d14, d15, [x1, #152]\n"
        "  ret\n"
        ".size co_ctx_swap, .-co_ctx_swap\n"
        "\n"
        ".balign 16\n"
        ".global co_ctx_start\n"
        ".type co_ctx_start, %function\n"
        "co_ctx_start:\n"  // a fresh context: its entry in x19; fp / lr zeroed so a backtrace stops here
        "  mov x29, #0\n"
        "  mov x30, #0\n"
        "  blr x19\n"
        "  bl co_ctx_returned\n"
        "  brk #0\n"
        ".size co_ctx_start, .-co_ctx_start\n");

void co_ctx_get(CoCtx *c) {
    // Nothing to capture: a context is only read after co_ctx_make filled it or a switch saved into it.
    // (On POSIX getcontext exists because makecontext needs an initialised context; here it does not.)
    memset(c, 0, sizeof *c);
}

void co_ctx_make(CoCtx *c, void (*entry)(void)) {
    c->entry = entry;
    // the stack grows down from its top, 16-byte aligned as AAPCS64 requires at a call
    uintptr_t top = (uintptr_t)c->uc_stack.ss_sp + c->uc_stack.ss_size;
    c->sp = (uint64_t)(top & ~(uintptr_t)15);
    c->x[0] = (uint64_t)(uintptr_t)entry;            // x19: co_ctx_start calls it
    c->x[10] = 0;                                     // x29: no frame above
    c->x[11] = (uint64_t)(uintptr_t)co_ctx_start;     // x30: the first switch "returns" there
}
