#!/bin/bash
# Pokémon Legends Z-A for Android: its game code generated at the Android base from the exefs import_za.sh made,
# then libmain.so and lasgame.so in a build folder of their own (build/android-za), with build_gamelib_android.sh's
# checks. Link's Awakening's tree and build (generated-android, build/android) are left as they are.
# Afterwards: GAME_BUILD=/root/lasrecomp/build/android-za make_project.sh, then build_apk.sh.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/root/lasrecomp}
ZA=${ZA:-/root/za-extracted}

[ -f "$ZA/exefs/main" ] || { echo "no $ZA/exefs/main: import_za.sh first"; exit 1; }
# ZA_MODS=<folder of exefs mods (.pchtxt)>: the game generated from a patched copy of its exefs (apply_pchtxt.py) -
# /root/za-modded, the original main kept beside as main.orig (gen.py: a patch site a mod took is left to it). Push
# that exefs to the phone too: the runtime loads the patched data from it. The mods are the player's own files.
if [ -n "${ZA_MODS:-}" ]; then
    MZ=${MZ:-/root/za-modded}
    rm -rf "$MZ" && mkdir -p "$MZ/exefs"
    for f in "$ZA"/exefs/*; do [ "$(basename "$f")" = main ] || cp "$f" "$MZ/exefs/"; done
    cp "$ZA/exefs/main" "$MZ/exefs/main.orig"
    "$WORK/tools/venv/bin/python" "$HERE/apply_pchtxt.py" "$ZA/exefs/main" "$MZ/exefs/main" "$ZA_MODS"
    ln -sfn "$ZA/romfs.bin" "$MZ/romfs.bin"
    ZA=$MZ
fi
cd "$WORK"
# the Makefile resolves EXEFS against the checkout; extracted/ is never synced or committed, so the link lives there
mkdir -p extracted
ln -sfn "$ZA" extracted/za

echo "=== generate Z-A at the Android base (generated-android-za) ==="
# (gen.py says "main: Pokémon Legends Z-A (build ...): N game patch(es)" here: the frame-mode site, moved to this base)
make gen PLATFORM=android EXEFS=extracted/za/exefs GEN=generated-android-za 2>&1 | grep -vE 'progress' | tail -8
echo "generated C files: $(find generated-android-za -name '*.c' | wc -l)"

echo "=== libmain.so + lasgame.so (build/android-za) ==="
GEN_DIR=generated-android-za BREL=build/android-za bash "$HERE/build_gamelib_android.sh" 2>&1 | tail -16
echo "ZA BUILD OK"
