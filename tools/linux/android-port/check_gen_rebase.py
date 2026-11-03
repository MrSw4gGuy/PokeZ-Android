"""Before regenerating: do the patch and hook sites land inside the main image at the Android base?

tools/patches.py builds HOOKS and PATCHES from game_addresses.ADDRESS on import, and gen.py only applies a site when
the address of the instruction it is lifting is a key. At the desktop base the keys are 0x70.. addresses; if they
are not moved to 0x20.. along with the module, nothing matches and the build silently has no patches or hooks.
Run from the repo root: python tools/linux/android-port/check_gen_rebase.py <exefs/main> <base>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/
import game_addresses as ga  # noqa: E402
from nso import Nso  # noqa: E402

main, base = sys.argv[1], int(sys.argv[2], 0)
nso = Nso(main)
ga.select_build_id(nso.build_id, base - ga.DESKTOP_MAIN)
import patches  # noqa: E402  (builds its tables from ADDRESS now)

lo, hi = base, base + len(nso.image)
sites = list(patches.HOOKS) + list(patches.PATCHES)
outside = [a for a in sites if not lo <= a < hi]
print(f'base {base:#x}: {len(patches.HOOKS)} hook sites, {len(patches.PATCHES)} patch sites')
print(f'main image {lo:#x}-{hi:#x}; sites outside it: {len(outside)}')
for a in outside[:5]:
    print(f'  {a:#x}')
# a hook names the instruction it expects: check it is really there at the moved address
wrong = [a for a, (word, _t, _x) in patches.HOOKS.items()
         if lo <= a < hi and int.from_bytes(nso.image[a - lo:a - lo + 4], 'little') != word]
print(f'hook sites whose instruction does not match: {len(wrong)}')
sys.exit(1 if outside or wrong else 0)
