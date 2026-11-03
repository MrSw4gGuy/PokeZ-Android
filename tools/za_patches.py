"""Pokémon Legends: Z-A's game patches (tools/gen.py: exefs/main builds in ZA_BUILDS), apart from tools/patches.py:
that one builds Link's Awakening's tables from its address table on import (tools/game_addresses.py), which has no
Z-A version."""
import struct

# Frame mode: the game's frame clock (f_70019e3730) reads its configured "vsyncs per frame - 1" byte (1 on the
# Switch: 30 fps) from [[x19, #0x40], #0x20]; it sets the present interval from it and hands every subsystem
# max(that, the vsyncs that really passed) as the frame's step. Through rt_za_frame_cfg the 60 fps option makes it 0:
# a present every vsync and a 1/60 s step - the world keeps its speed.
ZA_BUILDS = {'7222e13ecf6adb32': 0x70019e37a8,  # 1.0.0
             '8a1c86c437394b69': 0x7001b29cf8,  # 2.0.0 (Mega Dimension; the same function)
             'b1f12fd919eae86a': 0x7001b296d8}  # 2.0.2


def _za_frame_cfg(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x39c08134, hex(w)  # ldrsb w20, [x9, #0x20]
    return ('{ extern int32_t rt_za_frame_cfg(int32_t); '
            'c->x[20] = (uint32_t)rt_za_frame_cfg((int8_t)LD8(c->x[9] + 0x20)); }'
            f'  /* {a:#x}: ldrsb w20, [x9, #0x20] (frame mode) */')


# Graphics tweaks, each an F1 option that applies at once (runtime/hle/za_tweaks.c rt_za_tweak: the game's own value,
# or the tweak's). The sites and the values are those of Fl4sh9174's exefs mods for 2.0.2
# (github.com/Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods: "Level of Detail", "Disable DOF", "Disable FXAA", "Reduce
# Bloom"), made switchable here instead of patched in. Each site's instruction is checked, as the frame mode's.
#   which: 0 the level-of-detail bias, 1 depth-of-field intensity, 2 FXAA strength, 3 bloom intensity, 4 skip bloom
def _ld_s0(base_reg, off, which, what):
    def p(gen, a, base_reg=base_reg, off=off, which=which, what=what):
        return ('{ extern uint32_t rt_za_tweak(int, uint32_t); '
                f'c->v[0] = (__uint128_t)rt_za_tweak({which}, LD32({base_reg} + {off:#x})); }}'
                f'  /* {a:#x}: ldr s0, [...] ({what}: F1 tweak) */')
    return p


def _fdiv_s0(gen, a):  # fdiv s0, s1, s0 - the FXAA strength
    return ('{ extern uint32_t rt_za_tweak(int, uint32_t); union { float f; uint32_t u; } n_, d_, r_; '
            'n_.u = (uint32_t)c->v[1]; d_.u = (uint32_t)c->v[0]; r_.f = n_.f / d_.f; '
            'c->v[0] = (__uint128_t)rt_za_tweak(2, r_.u); }'
            f'  /* {a:#x}: fdiv s0, s1, s0 (FXAA: F1 tweak) */')


def _bloom_skip(gen, a):  # b.le past the bloom pass - always, when bloom is off
    return ('{ extern uint32_t rt_za_tweak(int, uint32_t); '
            f'if (rt_za_tweak(4, 0) || COND_LE(c)) goto L_{a + (0xf30f2c - 0xf30bdc):x}; }}'
            f'  /* {a:#x}: b.le (bloom: F1 tweak) */')


ZA_TWEAKS = {  # build: {desktop address: (its instruction, patch)}
    'b1f12fd919eae86a': {
        0x70018b58e4: (0xbd401e60, _ld_s0('c->x[19]', 0x1c, 0, 'LOD bias')),
        0x7000f31830: (0xbd4417e0, _ld_s0('c->sp', 0x414, 1, 'depth of field')),
        0x7000e6d134: (0x1e201820, _fdiv_s0),
        0x7000e6d154: (0x1e201820, _fdiv_s0),
        0x7000f30bd4: (0xbd411320, _ld_s0('c->x[25]', 0x110, 3, 'bloom intensity')),
        0x7000f30bdc: (0x54001a8d, _bloom_skip),
    },
}


def _checked(word, patch):
    def p(gen, a):
        w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
        assert w == word, f'{a:#x}: {w:#010x}, expected {word:#010x}'
        return patch(gen, a)
    return p


def za_patches(build):
    out = {ZA_BUILDS[build]: _za_frame_cfg}
    for a, (word, patch) in ZA_TWEAKS.get(build, {}).items():
        out[a] = _checked(word, patch)
    return out
