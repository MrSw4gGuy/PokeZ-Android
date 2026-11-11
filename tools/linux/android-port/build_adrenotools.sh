#!/bin/bash
# libadrenotools (third_party/libadrenotools, BSD-2-Clause) for the phone: the static library libmain.so links (custom
# Vulkan drivers such as Mesa's Turnip, loaded in-app: runtime/platform/vk_shim_android.c) and its hook libraries,
# which the APK carries in its lib folder (make_project.sh), where adrenotools' linker namespace looks for them.
#   build_adrenotools.sh [build dir]   (default: $WORK/build/android/adrenotools; skipped when already built)
set -euo pipefail
WORK=${WORK:-/root/lasrecomp}
OUT=${1:-$WORK/build/android/adrenotools}
NDK=${ANDROID_NDK:-/opt/android-ndk}
if [ -f "$OUT/libadrenotools.a" ] && [ -f "$OUT/src/hook/libmain_hook.so" ] && [ "${FORCE:-0}" = 0 ]; then
    echo "adrenotools: already built in $OUT"
    exit 0
fi
cmake -S "$WORK/third_party/libadrenotools" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-31 -DANDROID_STL=c++_static > "$OUT.cmake.log" 2>&1 \
    || { echo "FAIL: cmake"; tail -20 "$OUT.cmake.log"; exit 1; }
ninja -C "$OUT" > "$OUT.ninja.log" 2>&1 || { echo "FAIL: ninja"; tail -20 "$OUT.ninja.log"; exit 1; }
ls -l "$OUT/libadrenotools.a" "$OUT/lib/linkernsbypass/liblinkernsbypass.a" "$OUT"/src/hook/*.so | awk '{print $5, $9}'
echo "adrenotools OK"
