#!/bin/bash
# Build test_fiber.c with runtime/platform/android_fiber.c for the device (NDK, arm64), with a stand-in for the
# runtime's hle.h (FATAL only), into OUT. Push and run it with adb from the host:
#   adb push <OUT>/test_fiber /data/local/tmp/ && adb shell /data/local/tmp/test_fiber
set -euo pipefail
WORK=${WORK:-/root/lasrecomp}
OUT=${1:-/root/fibertest}
CC=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
rm -rf "$OUT" && mkdir -p "$OUT/platform"
cp "$WORK/runtime/platform/android_fiber.c" "$WORK/runtime/platform/android_fiber.h" "$OUT/platform/"
printf '#pragma once\n#include <stdio.h>\n#include <stdlib.h>\n#define FATAL(...) (fprintf(stderr, __VA_ARGS__), abort())\n' \
    > "$OUT/hle.h"  # (android_fiber.c includes "../hle.h")
"$CC" --target=aarch64-linux-android31 -O2 -fPIE -pie -I"$OUT/platform" \
    "$WORK/tools/linux/android-port/test_fiber.c" "$OUT/platform/android_fiber.c" -o "$OUT/test_fiber" -lm
file "$OUT/test_fiber" | cut -d, -f1-2
echo "built $OUT/test_fiber"
