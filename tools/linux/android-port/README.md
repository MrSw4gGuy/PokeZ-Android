# Android (arm64) build

Builds the runtime as an Android app, on a WSL2 Ubuntu 22.04 (or any x86-64 Linux), run as root. Tested on a
Galaxy S25+ (Android 17, Snapdragon 8 Elite / Adreno 830) with Link's Awakening 1.0.0.

**The APK carries the game code library (`lasgame.so`) generated from your own game files: it is Nintendo's code,
recompiled. Never share the APK or upload it anywhere** - the same rule as `generated/` and `extracted/`.

## Order

The scripts work on a copy of the checkout at `/root/lasrecomp` (they rsync it from the Windows clone; set `SRC` /
`WORK` for other paths). The checkout must have LF line endings (`git config core.autocrlf false`).

1. `setup_wsl.sh` - host toolchain: clang 21, arm64 multiarch libraries (for the aarch64 Linux build too).
2. `install_ndk.sh` (NDK r29: clang 21, as the desktop builds), `install_sdk.sh` (platform 35, build-tools; needs
   `openjdk-17-jdk-headless`). For Pokémon Legends Z-A's import also `dotnet-sdk-8.0` (tools/zaimport).
3. `tools/linux/build_deps.sh aarch64` once, for its pinned, checksummed source tarballs in `build/linux/src`, then
   `deps_android.sh all`: SDL3, shaderc (glslang / SPIRV-Tools linked in), LGPL FFmpeg, Vulkan + VMA headers.
4. The game code: your exefs in `extracted/exefs`, then `rebuild_relocated.sh` (`make gen PLATFORM=android` into
   `generated-android/`, a hook-count check against the desktop-base tree, then step 5).
5. `build_gamelib_android.sh` - `libmain.so` (the runtime) and `lasgame.so` (the game code) through the Makefile,
   checking the entry point, the id and that every symbol the game code needs is provided.
6. `make_project.sh`, `build_apk.sh` - the Gradle project from SDL3's Android template, all native code prebuilt.
7. `adb install -r` the APK; copy `extracted/` (exefs + romfs) to `/sdcard/Android/data/org.recomp.za/files/`.

Checks: `check_build_id.py` (is a dump's build ID supported), `check_gen_rebase.py` (patch / hook sites at a base),
`verify_deps.sh`, `verify_shaderc.sh`, `check_libcxx.sh` (nothing packaged needs libc++_shared).
The aarch64 Linux build (no Android): `build_aarch64.sh`, `gen_aarch64.sh`.

## What is different on Android

- The guest window is at 0x20_0000_0000, not 0x70_0000_0000: scudo, the platform allocator, owns that range from
  the start of every app process. Everything is written as desktop addresses and moved by `GUEST_SHIFT` (runtime/rt.h),
  the address tables and the generator's patch / hook sites included (tools/game_addresses.py).
- An app cannot start a child process, so the game code is built ahead of time here, never by the start screen
  (runtime/platform/android.c `plat_spawn`). It is named through `LARECOMP_GAME_LIB`.
- Rendering: Vulkan features are checked one by one against the driver (an Adreno lacks some a 1.3 desktop driver has),
  the swapchain is pre-rotated (the phone's native orientation is portrait), and the render scale steps down with
  Android's thermal headroom forecast (`[thermal]` log lines; `LARECOMP_THERMAL=0`: off).
- The free fonts the desktop ships beside the executable travel as APK assets and are unpacked to internal storage
  (`LARECOMP_RESOURCE_DIR`); Android's own Roboto lacks the glyph the game needs.
- nn::os fibers have no host implementation yet (runtime/platform/android_fiber.c): Link's Awakening never uses them,
  Pokémon Legends Z-A does.

The log is `lasrecomp.log` in the app's data folder above; after a crash, Android restarts the app and the crashed
run's log is `lasrecomp-backup-*-<pid>.log`.
