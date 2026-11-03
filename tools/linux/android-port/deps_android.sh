#!/bin/bash
# Android (arm64-v8a) builds of what the runtime links against, into build/android/deps.
#
# Three of the Linux dependencies change character here:
#   SDL3    - has a first-class Android backend; the same pinned 3.4.16 source as tools/linux/build_deps.sh.
#   shaderc - the NDK ships the sources, but not a built library; built from the same pinned tarball instead so
#             the SPIR-V it produces matches what the desktop build was tested with.
#   FFmpeg  - no Android support in its configure defaults; needs the cross triple spelled out. Done last because
#             it is the only one that is genuinely new work.
# Vulkan needs nothing: the NDK sysroot has the headers and the stub libvulkan.so, and the real driver is the
# device's own.
set -euo pipefail
NDK=${NDK:-/opt/android-ndk}
API=${API:-31}
ABI=arm64-v8a
WORK=${WORK:-/root/lasrecomp}
P=$WORK/build/android/deps
S=$WORK/build/linux/src          # the tarballs build_deps.sh already fetched and checksummed
W=$WORK/build/android/deps-build
T=$NDK/toolchains/llvm/prebuilt/linux-x86_64
JOBS=$(nproc)
WHAT=${1:-sdl}

mkdir -p "$P" "$W"
unpack() { [ -d "$W/$2" ] || { mkdir -p "$W/$2" && tar -xf "$S/$1" -C "$W/$2" --strip-components 1; }; }

cmake_android=(
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_LIBDIR=lib
)

# Vulkan and VMA are headers only: the Vulkan loader is the device's, and VMA is header-only. The NDK sysroot
# has a Vulkan header too, but an older one - the same pinned versions as the desktop build go here so the
# Vulkan backend compiles against what it was written for.
if [ "$WHAT" = headers ] || [ "$WHAT" = all ] || [ ! -f "$P/include/vma/vk_mem_alloc.h" ]; then
    if [ ! -f "$P/include/vulkan/vulkan.h" ]; then
        unpack Vulkan-Headers-1.4.350.1.tar.gz vulkan-headers
        mkdir -p "$P/include"
        cp -R "$W/vulkan-headers/include/vulkan" "$W/vulkan-headers/include/vk_video" "$P/include/"
    fi
    if [ ! -f "$P/include/vma/vk_mem_alloc.h" ]; then
        unpack VulkanMemoryAllocator-3.3.0.tar.gz vma
        mkdir -p "$P/include/vma" && cp "$W/vma/include/vk_mem_alloc.h" "$P/include/vma/"
    fi
    echo "Vulkan + VMA headers in place"
fi

if [ "$WHAT" = sdl ] || [ "$WHAT" = all ]; then
    if [ -f "$P/lib/libSDL3.so" ]; then
        echo "SDL3 already built"
    else
        echo "=== SDL3 for $ABI (API $API) ==="
        unpack SDL3-3.4.16.tar.gz sdl
        cmake -S "$W/sdl" -B "$W/sdl/build-android" -G Ninja "${cmake_android[@]}" \
            -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        cmake --build "$W/sdl/build-android" -j"$JOBS"
        cmake --install "$W/sdl/build-android"
    fi
    "$T/bin/llvm-readelf" -h "$P/lib/libSDL3.so" | grep -E "Machine|Class"
    echo "SDL3 Java glue the APK will need:"
    find "$W/sdl/android-project" -name "SDLActivity.java" 2>/dev/null | head -1 || echo "  (check the source tree)"
fi

if [ "$WHAT" = shaderc ] || [ "$WHAT" = all ]; then
    if [ -f "$P/lib/libshaderc_shared.so" ]; then
        echo "shaderc already built"
    else
        echo "=== shaderc for $ABI ==="
        unpack shaderc-2026.3.tar.gz shaderc
        (cd "$W/shaderc" && [ -d third_party/glslang ] || ./utils/git-sync-deps)
        # BUILD_SHARED_LIBS=OFF, exactly as tools/linux/build_deps.sh does it: glslang and SPIRV-Tools end up
        # inside libshaderc_shared.so. Built with it ON they become separate .so files that would each have to be
        # packaged (libglslang.so alone is 139 MB), and the app dies at startup on the first one it cannot find.
        cmake -S "$W/shaderc" -B "$W/shaderc/build-android" -G Ninja "${cmake_android[@]}" \
            -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
            -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DENABLE_GLSLANG_BINARIES=OFF \
            -DSPIRV_TOOLS_BUILD_STATIC=ON -DBUILD_SHARED_LIBS=OFF
        # this target only, and copy just this file: `cmake --install` would scatter the static pieces too
        cmake --build "$W/shaderc/build-android" --target shaderc_shared -j"$JOBS"
        mkdir -p "$P/include/shaderc"
        cp "$W/shaderc/libshaderc/include/shaderc/"*.h "$P/include/shaderc/"
        cp -P "$W/shaderc/build-android/libshaderc/libshaderc_shared.so"* "$P/lib/"
    fi
fi

if [ "$WHAT" = ffmpeg ] || [ "$WHAT" = all ]; then
    if [ -f "$P/lib/libavcodec.so" ]; then
        echo "FFmpeg already built"
    else
        echo "=== FFmpeg for $ABI ==="
        unpack ffmpeg-9.0.2.tar.xz ffmpeg
        # Same switches as tools/linux/build_deps.sh - only what the game's movies need, and LGPL only - with the
        # target changed. The NDK ships per-API wrapper scripts (aarch64-linux-android31-clang) that already carry
        # the sysroot and the target triple, which is what FFmpeg's configure expects of a cross compiler.
        (cd "$W/ffmpeg" && ./configure --prefix="$P" \
            --enable-cross-compile --target-os=android --arch=aarch64 \
            --cc="$T/bin/aarch64-linux-android$API-clang" \
            --cxx="$T/bin/aarch64-linux-android$API-clang++" \
            --ld="$T/bin/aarch64-linux-android$API-clang" \
            --nm="$T/bin/llvm-nm" --ar="$T/bin/llvm-ar" --ranlib="$T/bin/llvm-ranlib" --strip="$T/bin/llvm-strip" \
            --enable-shared --disable-static --enable-pic \
            --disable-autodetect --disable-programs --disable-doc --disable-network --disable-avdevice --disable-avfilter \
            --disable-swresample --disable-everything \
            --enable-protocol=file --enable-demuxer=matroska,mov \
            --enable-decoder=vp9,vp8,vorbis,opus,h264,aac --enable-parser=vp9,vp8,vorbis,opus,h264,aac \
            && make -j"$JOBS" && make install)
    fi
    # the same licence check the Linux build does: a GPL FFmpeg would make the whole app GPL
    grep -q 'GPL version 3\|--enable-gpl' "$P/lib/pkgconfig/libavcodec.pc" && { echo "FFmpeg is not LGPL"; exit 1; }
    echo "FFmpeg is LGPL, as on the desktop"
fi

echo
echo "=== $P/lib ==="
ls -la "$P/lib" 2>/dev/null | grep -E "\.so|\.a" || echo "(nothing yet)"
