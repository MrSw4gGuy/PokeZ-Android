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
# and every symbol it leaves undefined must come from a library it names (the NDK's system ones, the deps): an upstream
# merge brought plat_is_link (a platform call android.c lacked) and zlib's inflateInit2_ (no -lz yet) - both linked
# fine and failed only at the phone's dlopen ("cannot locate symbol"). make -B on the link: flags aren't prerequisites
SR=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/31
NEED=$("$N/llvm-readelf" -d "$BREL/libmain.so" | grep -oE 'NEEDED.*\[[^]]+\]' | grep -oE '\[[^]]+\]' | tr -d '[]')
PROV=$(for l in $NEED; do f="$SR/$l"; [ -f "$f" ] || f="$WORK/build/android/deps/lib/$l"; [ -f "$f" ] && "$N/llvm-nm" -D --defined-only "$f"; done 2>/dev/null \
       | awk '{print $NF}' | sed 's/@.*//' | sort -u)
MISS=$(comm -23 <("$N/llvm-nm" -D --undefined-only "$BREL/libmain.so" | awk '{print $NF}' | sed 's/@.*//' | sort -u) <(echo "$PROV"))
[ -z "$MISS" ] || { echo "FAIL: libmain.so needs symbols no library it names provides:"; echo "$MISS" | head -20; exit 1; }
echo "libmain.so undefined symbols: all provided"

echo "=== project + APK ==="
GAME_BUILD="$WORK/$BREL" bash "$HERE/make_project.sh" | tail -12
bash "$HERE/build_apk.sh" | tail -14
cp "$(find "$WORK/build/android/app/app/build/outputs/apk" -name '*.apk' | head -1)" "$OUT"
echo "copied to $OUT"
echo "ZA APK OK"
