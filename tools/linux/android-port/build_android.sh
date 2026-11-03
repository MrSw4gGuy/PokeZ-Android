#!/bin/bash
# The whole runtime for Android, through the Makefile's PLATFORM=android block.
#
# Everything this needs now exists: the NDK (clang 21), SDL3 / shaderc / FFmpeg cross-built into
# build/android/deps, runtime/platform/android.c, and the Makefile target. What is being tested here is the
# other ~120 runtime sources against bionic - android.c was only the part written for it on purpose.
set -euo pipefail
WORK=${WORK:-/root/lasrecomp}
SRC=${SRC:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp"}
P=$WORK/build/android/deps
JOBS=$(nproc)

say() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }

say "sync the checkout"
rsync -a --delete -c --exclude=.git --exclude=build --exclude=extracted --exclude=tools/venv --exclude='generated-*' "$SRC/" "$WORK/"
cd "$WORK"

say "compile the runtime for Android"
# -k so one pass reports every file bionic rejects, instead of stopping at the first
make -k -j"$JOBS" PLATFORM=android DEPS="$P" runtime-objs > /root/android_rt.log 2>&1 || true

errs=$(grep -c "error:" /root/android_rt.log || true)
objs=$(find build/android/rt -name '*.o' 2>/dev/null | wc -l)
echo "objects: $objs    error lines: $errs"
if [ "$errs" != 0 ]; then
    echo
    echo "--- distinct errors ---"
    grep -oE "error: .*" /root/android_rt.log | sort | uniq -c | sort -rn | head -20
    echo
    echo "--- files that failed ---"
    grep -oE "^[a-zA-Z0-9_/.-]+\.(c|cpp|mm):" /root/android_rt.log | sort -u | head -20
    exit 1
fi

say "link libmain.so"
make -j"$JOBS" PLATFORM=android DEPS="$P" build/android/libmain.so 2>&1 | tail -5
ls -lh build/android/libmain.so | cut -d' ' -f5-
/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -h build/android/libmain.so | grep -E "Class|Machine|Type"
echo
echo "--- what it will need on the device ---"
/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -d build/android/libmain.so | grep NEEDED
echo "ANDROID RUNTIME OK"
