#!/bin/bash
# After the GUEST_SHIFT relocation: regenerate the Android game code (its patch and hook sites now match), rebuild
# libmain.so and lasgame.so, and prove the hooks made it in by counting them against the desktop-base tree.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/root/lasrecomp}

say() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }

say "sync + regenerate generated-android"
rsync -a --delete -c --exclude=.git --exclude=build --exclude=extracted --exclude=tools/venv --exclude='generated-*' \
    "/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp/" "$WORK/"
cd "$WORK"
make gen PLATFORM=android 2>&1 | grep -vE 'progress' | tail -6

say "hooks in the generated code (they were all missing before)"
a=$(grep -rho 'rt_hook(c, ' generated-android | wc -l)
d=$(grep -rho 'rt_hook(c, ' generated-aarch64 2>/dev/null | wc -l)
echo "android (base 0x20..): $a    desktop-base tree (0x70..): $d"
[ "$a" -gt 0 ] && [ "$a" = "$d" ] || { echo "FAIL: hook count differs"; exit 1; }
# (|| true: head closes the pipe early, grep dies of SIGPIPE, and pipefail + set -e would end the script here)
grep -rhoE 'rt_hook\(c, 0x[0-9a-f]+\)' generated-android | head -2 || true

say "libmain.so + lasgame.so, with the three checks"
bash "$HERE/build_gamelib_android.sh" 2>&1 | tail -16
echo "RELOCATED BUILD OK"
