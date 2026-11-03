#!/bin/bash
# Host toolchain for the aarch64 cross-build, on a fresh Ubuntu 22.04 (WSL2). Run as root.
#
# Instead of tools/linux/Containerfile's approach - export a whole arm64 container image as the sysroot - this
# uses dpkg multiarch: the arm64 -dev packages install alongside the amd64 ones under /usr/lib/aarch64-linux-gnu,
# so the sysroot is just "/" and nothing ever runs under QEMU. 22.04 to match the AppImage's glibc 2.35 target.
set -euo pipefail

echo "=== multiarch: amd64 from archive.ubuntu.com, arm64 from ports.ubuntu.com ==="
# the main archive carries no arm64, and ports carries no amd64: each needs its own arch filter or apt update errors
sed -i 's|^deb http|deb [arch=amd64] http|' /etc/apt/sources.list
cat > /etc/apt/sources.list.d/arm64-ports.list <<'EOF'
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-security main restricted universe multiverse
EOF
dpkg --add-architecture arm64
export DEBIAN_FRONTEND=noninteractive
apt-get update

echo "=== host build tools (the same set as tools/linux/Containerfile) ==="
apt-get install -y --no-install-recommends \
    ca-certificates curl git file xz-utils patchelf make cmake ninja-build pkg-config nasm python3 python3-venv g++

echo "=== clang 21 from apt.llvm.org (22.04's clang 15 lacks the __builtin_elementwise_* in simd_fp.h) ==="
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-21 main" > /etc/apt/sources.list.d/llvm.list
apt-get update
apt-get install -y --no-install-recommends clang-21 lld-21 llvm-21
for t in clang clang++ ld.lld llvm-ar llvm-nm llvm-ranlib llvm-strip; do ln -sf $t-21 /usr/bin/$t; done

echo "=== aarch64 cross toolchain (crt files, libgcc, libstdc++ for the target) ==="
apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

echo "=== arm64 target libraries: SDL3's backends and Vulkan, same list as the Containerfile ==="
apt-get install -y --no-install-recommends \
    libx11-dev:arm64 libxext-dev:arm64 libxrandr-dev:arm64 libxcursor-dev:arm64 libxi-dev:arm64 \
    libxss-dev:arm64 libxfixes-dev:arm64 libxtst-dev:arm64 libxkbcommon-dev:arm64 libwayland-dev:arm64 \
    libdecor-0-dev:arm64 libegl-dev:arm64 libgl-dev:arm64 libdrm-dev:arm64 libgbm-dev:arm64 \
    libpulse-dev:arm64 libasound2-dev:arm64 libpipewire-0.3-dev:arm64 libdbus-1-dev:arm64 \
    libudev-dev:arm64 libibus-1.0-dev:arm64 libvulkan-dev:arm64 \
    wayland-protocols

echo
echo "=== versions ==="
clang --version | head -1
aarch64-linux-gnu-gcc --version | head -1
cmake --version | head -1
echo "arm64 packages installed: $(dpkg -l | grep -c ':arm64')"
echo "SETUP OK"
