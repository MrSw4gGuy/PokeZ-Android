#!/bin/bash
# Build test_ahb_gmem.c for the device (NDK, arm64) into OUT. Run it from the host:
#   adb push <OUT>/test_ahb_gmem /data/local/tmp/ && adb shell /data/local/tmp/test_ahb_gmem
set -euo pipefail
SRC=${SRC:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp"}
OUT=${1:-/root/ahbtest}
CC=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
mkdir -p "$OUT"
"$CC" --target=aarch64-linux-android31 -O1 -fPIE -pie "$SRC/tools/linux/android-port/test_ahb_gmem.c" \
    -o "$OUT/test_ahb_gmem" -lvulkan -landroid
echo "built $OUT/test_ahb_gmem"
