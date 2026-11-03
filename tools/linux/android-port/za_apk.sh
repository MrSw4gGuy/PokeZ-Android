#!/bin/bash
# The Z-A APK after a runtime change: libmain.so rebuilt in build/android-za (lasgame.so, from build_za.sh, is left as
# it is - a runtime change does not touch the game code), then the project and the APK with that build folder's two
# libraries. The APK carries game code generated from the player's own dump: it is for their phone only.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/root/lasrecomp}
OUT=${OUT:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/ZARecomp-za-debug.apk"}
BREL=build/android-za
N=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin

[ -f "$WORK/$BREL/lasgame.so" ] || { echo "no $BREL/lasgame.so: build_za.sh first"; exit 1; }

echo "=== sync + compile check (build_android.sh) ==="
bash "$HERE/build_android.sh" 2>&1 | grep -E "objects:|error:|RUNTIME OK"
cd "$WORK"

echo "=== libmain.so ($BREL) ==="
make -j"$(nproc)" PLATFORM=android DEPS="$WORK/build/android/deps" GEN=generated-android-za B="$BREL" "$BREL/libmain.so" \
    > /root/za_libmain.log 2>&1 || { echo "FAIL: make"; tail -20 /root/za_libmain.log; exit 1; }
ls -lh "$BREL/libmain.so" | sed 's/.* \([0-9.]*[MK]\) .*/size: \1/'
# a -shared link leaves undefined symbols for load time: one the overlay calls into the menu would only show up as a
# dlopen failure on the phone, so check here that it is defined
# (the list read whole first: grep -q stopping early would SIGPIPE llvm-nm, and pipefail call that a miss)
DEF=$("$N/llvm-nm" -D --defined-only "$BREL/libmain.so")
for s in menu_toggle_from_touch touch_overlay_event touch_overlay_draw; do
    grep -w "$s" <<<"$DEF" >/dev/null || { echo "FAIL: $s not defined in libmain.so"; exit 1; }
done
echo "touch overlay symbols: defined"

echo "=== project + APK ==="
GAME_BUILD="$WORK/$BREL" bash "$HERE/make_project.sh" | tail -12
bash "$HERE/build_apk.sh" | tail -14
cp "$(find "$WORK/build/android/app/app/build/outputs/apk" -name '*.apk' | head -1)" "$OUT"
echo "copied to $OUT"
echo "ZA APK OK"
