#!/bin/bash
# Generate the game code for aarch64 from your own extracted game, then compile it.
#
# This is the part the cross-build could not reach: `make gen` lifts the game's ARM64 into C, and for aarch64 the
# Makefile passes --simd asm (native NEON) instead of the --simd c path that x86-64 uses. That asm path is what
# docs/linux.md calls experimental and is the last unverified piece of the ARM64 story.
#
# Only exefs is read here. romfs matters when the game runs, not when its code is generated.
set -euo pipefail
GAME=${GAME:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/extracted"}
WORK=${WORK:-/root/lasrecomp}
ARCH=aarch64
JOBS=$(nproc)

say() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }

[ -d "$GAME/exefs" ] || { echo "no exefs at $GAME/exefs"; exit 1; }
cd "$WORK"

say "copy exefs onto ext4 (gen.py does heavy random reads; 9p would crawl)"
# exefs only: tens of MB. The Makefile resolves EXEFS relative to the repo root, so it has to live inside the tree.
mkdir -p extracted
rsync -a --delete "$GAME/exefs/" extracted/exefs/
du -sh extracted/exefs; ls -la extracted/exefs | head

say "which game is this, and is its build ID supported?"
# versions.h refuses an unknown build ID outright, so check before spending minutes on the lift
tools/venv/bin/python - <<'PY'
import pathlib, re
main = pathlib.Path("extracted/exefs/main")
bid = main.read_bytes()[0x40:0x54].hex().upper()
print("exefs/main build ID:", bid)
known = re.findall(r'GA_VERSION\((\w+),\s*"([^"]+)",\s*"([0-9A-F]+)"\)',
                   pathlib.Path("runtime/game_addresses/versions.h").read_text())
for key, label, want in known:
    print(f"  table {key:8} ({label:16}) {want}  {'<== MATCH' if bid.startswith(want) else ''}")
if not any(bid.startswith(w) for *_, w in known):
    raise SystemExit("\nthis build ID has no address table: the generator would refuse it")
PY

say "generate the game code (--simd asm for aarch64) - a few minutes, several GB of RAM"
make gen ARCH="$ARCH"

say "what came out"
ls generated-$ARCH/main | head -5
echo "generated C files: $(find generated-$ARCH -name '*.c' | wc -l)"
du -sh generated-$ARCH

say "compile it - the real ARM64 codegen test (slow: giant lifted functions)"
make -j"$JOBS" ARCH="$ARCH" SYSROOT=/ DEPS="$WORK/build/linux/$ARCH/deps" SHADERC=-lshaderc_shared gen-objs

say "link the game library"
make -j"$JOBS" ARCH="$ARCH" SYSROOT=/ DEPS="$WORK/build/linux/$ARCH/deps" SHADERC=-lshaderc_shared \
    "build/$ARCH/lasgame.so"
file "build/$ARCH/lasgame.so"
echo
echo "GEN OK"
