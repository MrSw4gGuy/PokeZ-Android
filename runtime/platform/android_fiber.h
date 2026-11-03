// Host context switching for runtime/hle/fiber.c on Android.
//
// bionic has no ucontext switch on arm64: the NDK's <ucontext.h> declares getcontext / makecontext / swapcontext,
// but libc.so for aarch64 exports none of them (llvm-nm -D finds zero). The declarations are a header-only
// promise. So the fiber backend fiber.c uses on POSIX is provided here (android_fiber.c).
//
// The field names mirror ucontext_t's (uc_stack.ss_sp, uc_stack.ss_size, uc_link) so fiber.c's body reads the same
// on every platform; only the three operations are named differently, through co_ctx_get / _make / _swap.
//
// co_ctx_swap is a hand-written aarch64 routine: it saves the AAPCS64 callee-saved state into one context, loads the
// other's and returns - into that context's caller, or the first time into co_ctx_start, which calls its entry.
// The offsets the routine uses are pinned by static asserts in android_fiber.c: keep the layout below as it is.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct CoCtx {
    uint64_t x[12];  // x19-x28, x29 (fp), x30 (lr)   (offset 0)
    uint64_t sp;     //                                (offset 96)
    uint64_t d[8];   // d8-d15                         (offset 104)
    void (*entry)(void);  // set by co_ctx_make, entered the first time this context is switched to
    struct {
        void *ss_sp;
        size_t ss_size;
    } uc_stack;
    struct CoCtx *uc_link;
} CoCtx;

void co_ctx_get(CoCtx *c);
void co_ctx_make(CoCtx *c, void (*entry)(void));
void co_ctx_swap(CoCtx *save, CoCtx *to);  // (assembly: android_fiber.c)
