#!/bin/bash
# What the Android build has to link against, and whether each piece is really arm64.
set -euo pipefail
L=${1:-/root/lasrecomp/build/android/deps/lib}
READELF=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

shopt -s nullglob
libs=("$L"/*.so)
if [ ${#libs[@]} -eq 0 ]; then echo "nothing in $L"; exit 1; fi

printf '%-38s %10s  %s\n' LIBRARY SIZE MACHINE
for f in "${libs[@]}"; do
    m=$("$READELF" -h "$f" | grep Machine | sed 's/.*Machine: *//')
    printf '%-38s %10s  %s\n' "$(basename "$f")" "$(du -h "$f" | cut -f1)" "$m"
done

echo
echo "still missing for a full link:"
for want in libSDL3 libshaderc_shared libavformat libavcodec libavutil libswscale; do
    [ -e "$L/$want.so" ] || echo "  $want"
done
echo "(libvulkan comes from the device, through the NDK sysroot stub - not built here)"
