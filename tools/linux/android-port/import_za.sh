#!/bin/bash
# Pokémon Legends Z-A from your own dump: tools/zaimport (.NET 8) turns the packages and prod.keys into
# <out>/exefs/ (the update's), <out>/romfs.bin (base + update) and <out>/aoc/ (DLC). The packages are found by
# name in SRC_DIR, so the script does not need their exact (accented, bracketed) file names.
# Usage: [EXCLUDE=<regex>] import_za.sh [SRC_DIR] [OUT_DIR]
set -euo pipefail
WORK=${WORK:-/root/lasrecomp}
SRC_DIR=${1:-"/mnt/d/Claude/Recomp/Pokemon Legends ZA/za-source"}
OUT=${2:-/root/za-extracted}
BIN=/root/zaimport-bin
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

if [ ! -x "$BIN/zaimport" ]; then
    echo "=== building zaimport ==="
    dotnet publish "$WORK/tools/zaimport/zaimport.csproj" -c Release -o "$BIN" 2>&1 | tail -3
fi

keys=$(find "$SRC_DIR" -maxdepth 1 -iname 'prod.keys' | head -1)
[ -n "$keys" ] || { echo "no prod.keys in $SRC_DIR"; exit 1; }
# EXCLUDE=<regex>: packages left out by name - e.g. a DLC dumped without its ticket, which zaimport cannot open
# without title.keys (the game runs without it)
mapfile -t pkgs < <(find "$SRC_DIR" -maxdepth 1 \( -iname '*.nsp' -o -iname '*.xci' \) | sort |
                    { if [ -n "${EXCLUDE:-}" ]; then grep -viE "$EXCLUDE" || true; else cat; fi; })
[ ${#pkgs[@]} -gt 0 ] || { echo "no .nsp / .xci in $SRC_DIR"; exit 1; }
echo "=== packages ==="
for p in "${pkgs[@]}"; do printf '  %8s  %s\n' "$(du -h "$p" | cut -f1)" "$(basename "$p")"; done

echo "=== zaimport -> $OUT ==="
mkdir -p "$OUT"
"$BIN/zaimport" "$keys" "$OUT" "${pkgs[@]}"

echo "=== result ==="
ls -la "$OUT" "$OUT/exefs"
python3 "$WORK/tools/linux/android-port/check_build_id.py" "$OUT/exefs" 2>/dev/null \
    || echo "(not a Link's Awakening build: expected for Z-A, which needs no address table)"
