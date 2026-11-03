#!/bin/bash
# Build test_turnip.c for the device (NDK, arm64) into OUT. Run it from the host, with a Turnip build pushed beside it:
#   adb push <OUT>/test_turnip <driver>.so /data/local/tmp/ && adb shell /data/local/tmp/test_turnip /data/local/tmp/<driver>.so
set -euo pipefail
SRC=${SRC:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp"}
OUT=${1:-/root/turniptest}
CC=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
mkdir -p "$OUT"
"$CC" --target=aarch64-linux-android31 -O1 -fPIE -pie "$SRC/tools/linux/android-port/test_turnip.c" -o "$OUT/test_turnip" -ldl
echo "built $OUT/test_turnip"
