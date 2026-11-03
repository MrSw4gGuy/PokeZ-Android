// Pokémon Z-A's graphics tweaks: the game code calls this at the sites tools/za_patches.py ZA_TWEAKS patches (2.0.2),
// with the value the game itself would use there, and gets back that value or the tweak's - per the F1 options, read
// at every call, so a change applies at once. The sites and values come from Fl4sh9174's exefs mods
// (github.com/Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods), made switchable here: a tweak that clashes with a scene
// can be turned off mid-game.
//   0 level-of-detail bias (a float: -1 = the mod's "improved LOD", fewer models swapped for simpler ones at a
//     distance - less pop-in, more GPU time)
//   1 depth-of-field intensity, 2 FXAA strength, 3 bloom intensity (0 = the effect off)
//   4 skip the bloom pass (returns 1: the branch past it is taken)
#include <stdint.h>

#include "../settings.h"

uint32_t rt_za_tweak(int which, uint32_t game_value) {
    switch (which) {
    case 0: return g_settings.za_lod == 1 ? 0xbf800000u : game_value;  // (-1.0f)
    case 1: return g_settings.za_dof_off ? 0 : game_value;
    case 2: return g_settings.za_fxaa_off ? 0 : game_value;
    case 3: return g_settings.za_bloom_off ? 0 : game_value;
    case 4: return g_settings.za_bloom_off ? 1 : 0;
    default: return game_value;
    }
}
