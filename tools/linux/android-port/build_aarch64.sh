#!/bin/bash
# The feasibility check: does the runtime compile and link for aarch64?
# Run as root inside the WSL2 Ubuntu 22.04 prepared by setup_wsl.sh.
#
# Sysroot is "/" because setup_wsl.sh installed the arm64 target libraries through dpkg multiarch
# (/usr/lib/aarch64-linux-gnu), rather than exporting an arm64 container image the way appimage.sh does.
#
# The build runs on ext4, not on /mnt/d: a 9p mount makes a build this size several times slower, and the
# dependency builds create symlinks that do not survive on NTFS.
set -euo pipefail
SRC=${SRC:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp"}
WORK=${WORK:-/root/lasrecomp}
ARCH=aarch64
export SYSROOT=${SYSROOT:-/}
JOBS=$(nproc)

say() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }

say "sync the checkout onto ext4"
mkdir -p "$WORK"
# -c: compare by checksum, since 9p timestamps are not reliable enough to drive rsync's default heuristic
rsync -a --delete -c --exclude=.git --exclude=build --exclude=extracted --exclude=tools/venv --exclude=generated-'*' \n    "$SRC/" "$WORK/"
cd "$WORK"
echo "at $WORK ($(du -sh . | cut -f1))"

say "dependencies for $ARCH: SDL3, FFmpeg, shaderc (pinned sources, cross-compiled)"
tools/linux/build_deps.sh "$ARCH"
P=$WORK/build/linux/$ARCH/deps
ls -la "$P/lib" | head

say "compile the runtime for $ARCH (no game code involved)"
# -k: keep going after a failure, so one pass reports every broken file instead of only the first
make -k -j"$JOBS" ARCH="$ARCH" SYSROOT="$SYSROOT" DEPS="$P" SHADERC=-lshaderc_shared runtime-objs \n    2>&1 | tee /root/compile.log || true
echo "--- compile errors ---"
grep -E "error:" /root/compile.log | sort -u || echo "(none)"
echo "$(grep -cE "error:" /root/compile.log || true) error lines"
if grep -q "error:" /root/compile.log; then echo "COMPILE FAILED - stopping before the link"; exit 1; fi

say "link the executable"
make -j"$JOBS" ARCH="$ARCH" SYSROOT="$SYSROOT" DEPS="$P" SHADERC=-lshaderc_shared "build/$ARCH/lasrecomp"

say "what came out"
file "build/$ARCH/lasrecomp"
echo "--- it must not have picked up any x86 instructions ---"
aarch64-linux-gnu-objdump -d --section=.text "build/$ARCH/lasrecomp" | head -12
echo "--- shared library dependencies ---"
aarch64-linux-gnu-objdump -p "build/$ARCH/lasrecomp" | grep NEEDED
echo
echo "BUILD OK: build/$ARCH/lasrecomp"
