#!/bin/bash
# Android NDK r29 into /opt/android-ndk. r29 carries the newest clang, which matters here: the Containerfile
# pins clang 21 on Linux because the lifted FP code in runtime/simd_fp.h uses __builtin_elementwise_* that
# older clangs lack, so the NDK's compiler has to be new enough to take the same generated code.
set -euo pipefail
NDK_VER=${NDK_VER:-r29}
DEST=${DEST:-/opt/android-ndk}
ZIP=/root/android-ndk-$NDK_VER-linux.zip

if [ -d "$DEST/toolchains/llvm/prebuilt/linux-x86_64" ]; then
    echo "already installed at $DEST"
else
    [ -f "$ZIP" ] || curl -fL --progress-bar -o "$ZIP.part" \
        "https://dl.google.com/android/repository/android-ndk-$NDK_VER-linux.zip" && mv -f "$ZIP.part" "$ZIP"
    echo "unzipping $(du -h "$ZIP" | cut -f1)..."
    rm -rf /root/ndk-unzip && mkdir -p /root/ndk-unzip
    unzip -q "$ZIP" -d /root/ndk-unzip
    rm -rf "$DEST" && mv /root/ndk-unzip/android-ndk-$NDK_VER "$DEST"
    rmdir /root/ndk-unzip
fi

T=$DEST/toolchains/llvm/prebuilt/linux-x86_64
echo "=== NDK $NDK_VER at $DEST ==="
"$T/bin/clang" --version | head -2
echo "=== API levels available for arm64 ==="
ls "$T/sysroot/usr/lib/aarch64-linux-android" | grep -E '^[0-9]+$' | sort -n | tail -5
echo "=== does it ship shaderc? ==="
find "$DEST" -name "libshaderc*" 2>/dev/null | head -3 || echo "(no shaderc in the NDK)"
echo "=== vulkan header / stub ==="
ls "$T/sysroot/usr/include/vulkan/vulkan.h" 2>/dev/null && echo "vulkan headers: yes"
find "$T/sysroot/usr/lib/aarch64-linux-android" -name "libvulkan.so" | head -2
echo "NDK OK"
