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
