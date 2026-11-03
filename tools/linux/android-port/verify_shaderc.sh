#!/bin/bash
# libshaderc_shared.so must carry glslang and SPIRV-Tools inside it. Built the other way they become separate
# .so files, and the app dies at startup on the first one missing from the APK - which is exactly what happened.
set -euo pipefail
R=/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
L=/root/lasrecomp/build/android/deps/lib

echo "libshaderc_shared.so: $(du -h "$L/libshaderc_shared.so" | cut -f1)"
echo
echo "what it needs at run time:"
"$R" -d "$L/libshaderc_shared.so" | grep NEEDED | sed 's/.*\[/  /; s/\]//'
echo
if "$R" -d "$L/libshaderc_shared.so" | grep -qE "libSPIRV|libglslang"; then
    echo "FAIL: still depends on a separate glslang / SPIRV-Tools"
    exit 1
fi
echo "OK: glslang and SPIRV-Tools are linked in"
echo
echo "everything that will be packaged:"
for f in "$L"/*.so; do printf '  %-24s %s\n' "$(basename "$f")" "$(du -h "$f" | cut -f1)"; done
