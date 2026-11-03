#!/bin/bash
# libmain.so, then lasgame.so through the Makefile's own rule - not by calling clang on the generated C directly,
# an earlier shortcut that skipped tools/gamelib_entry.c: the library had no lasgame_tables
# for the runtime to dlsym, no matching id, and nothing telling the dynamic linker where its runtime calls go.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/root/lasrecomp}
P=$WORK/build/android/deps
B=$WORK/build/android
N=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
JOBS=$(nproc)

say() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }

say "runtime (libmain.so)"
bash "$HERE/build_android.sh" 2>&1 | grep -E "objects:|error:|RUNTIME OK"
cd "$WORK"

say "game code (lasgame.so) via the Makefile"
rm -f "$B"/obj/*.o            # (that shortcut's flattened objects: not what the Makefile builds or links)
rm -f "$B/lasgame.so" "$B/lasgame.so.id"
# relative target: the Makefile's rule is $(B)/lasgame.so with B=build/android, and make does not consider an
# absolute spelling of the same file to be that target ("No rule to make target")
make -j"$JOBS" PLATFORM=android DEPS="$P" build/android/lasgame.so > /root/gamelib_android.log 2>&1 \
    || { echo "FAIL: make"; tail -20 /root/gamelib_android.log; exit 1; }
ls -lh "$B/lasgame.so" | sed 's/.* \([0-9.]*[MK]\) .*/size: \1/'

say "check 1: the entry point the runtime dlsym's"
"$N/llvm-nm" -D --defined-only "$B/lasgame.so" | grep -w lasgame_tables || { echo "FAIL: no lasgame_tables"; exit 1; }

say "check 2: the id the runtime will compare against"
echo "lasgame.so.id: $(cat "$B/lasgame.so.id")"
echo "embedded:      $(strings -n 20 "$B/lasgame.so" | grep -m1 -E '^[0-9a-f]{16}-[0-9A-F]{16}$' || echo '(not found)')"

say "check 3: does it name libmain.so, and is every symbol it needs actually provided?"
"$N/llvm-readelf" -d "$B/lasgame.so" | grep NEEDED | sed 's/.*\[/  /; s/\]//'
SYS=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/31
{ "$N/llvm-nm" -D --defined-only "$B/libmain.so"
  for l in libc.so libm.so libdl.so; do "$N/llvm-nm" -D --defined-only "$SYS/$l"; done
} | awk '{print $NF}' | sed 's/@.*//' | sort -u > /tmp/provided.txt
"$N/llvm-nm" -D --undefined-only "$B/lasgame.so" | awk '{print $NF}' | sed 's/@.*//' | sort -u > /tmp/needed.txt
missing=$(comm -23 /tmp/needed.txt /tmp/provided.txt)
echo "undefined in lasgame.so: $(wc -l < /tmp/needed.txt), unresolved: $(printf '%s' "$missing" | grep -c . || true)"
if [ -n "$missing" ]; then echo "FAIL - nobody provides:"; echo "$missing" | head -15 | sed 's/^/  /'; exit 1; fi
echo "GAMELIB OK"
