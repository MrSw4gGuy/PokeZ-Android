#!/bin/bash
# Which of the packaged libraries actually pull in libc++_shared.so.
#
# Why it matters: getLibraries() loads it first and Android loads with RTLD_GLOBAL, so the NDK's libc++ is in the
# process before HWUI brings up Vulkan. The crash is inside Qualcomm's snapalloc parser on a std::string/std::map
# call, with the platform's own libc++ (abi tag nn220000) in the frame above it. If only our own library needs
# libc++, linking it statically removes the shared copy from the process entirely.
set -euo pipefail
R=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
J=/root/lasrecomp/build/android/app/app/src/main/jniLibs/arm64-v8a

for f in "$J"/*.so; do
    n=$(basename "$f")
    [ "$n" = libc++_shared.so ] && continue
    if "$R" -d "$f" 2>/dev/null | grep -q "libc++_shared"; then
        echo "  NEEDS libc++_shared : $n"
    else
        echo "  self-contained      : $n"
    fi
done
