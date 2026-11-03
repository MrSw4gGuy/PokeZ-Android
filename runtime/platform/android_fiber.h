// Host context switching for runtime/hle/fiber.c on Android.
//
// bionic has no ucontext switch on arm64: the NDK's <ucontext.h> declares getcontext / makecontext / swapcontext,
// but libc.so for aarch64 exports none of them (llvm-nm -D finds zero). The declarations are a header-only
// promise. So the fiber backend fiber.c uses on POSIX has to be provided here.
//
// The field names mirror ucontext_t's (uc_stack.ss_sp, uc_stack.ss_size, uc_link) so fiber.c's body reads the same
// on every platform; only the three operations are named differently, through co_ctx_get / _make / _swap.
//
// STATUS: the switch itself is not implemented yet - co_ctx_swap aborts with a clear message if a game actually
// reaches it. That is deliberate and temporary:
//   - Link's Awakening never calls into fiber.c (nothing else in runtime/ calls it; the file is only in the build
//     because RT_SRC is a wildcard over runtime/hle/*.c), so an LA build links and runs with this in place.
//   - Pokémon Z-A runs its job system on fibers and will hit this immediately.
// Replacing it means writing one aarch64 routine: save x19-x28, x29, x30, sp and d8-d15 into the current context,
// load them from the other, and return. That is the whole AAPCS64 callee-saved set. Nothing in fiber.c changes.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct CoCtx {
    // the callee-saved state a switch has to carry: x19-x28, x29 (fp), x30 (lr), sp, then d8-d15
    uint64_t x[12];
    uint64_t sp;
    uint64_t d[8];
    void (*entry)(void);  // set by co_ctx_make, entered the first time this context is switched to
    struct {
        void *ss_sp;
        size_t ss_size;
    } uc_stack;
    struct CoCtx *uc_link;
} CoCtx;

void co_ctx_get(CoCtx *c);
void co_ctx_make(CoCtx *c, void (*entry)(void));
void co_ctx_swap(CoCtx *save, CoCtx *to);
