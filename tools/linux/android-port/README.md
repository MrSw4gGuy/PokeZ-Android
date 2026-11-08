# Android (arm64) build

Builds the runtime as an Android app, on a WSL2 Ubuntu 22.04 (or any x86-64 Linux), run as root. Tested on a
Galaxy S25+ (Android 17, Snapdragon 8 Elite / Adreno 830, Qualcomm's own Vulkan driver) with Pokémon Legends: Z-A 2.0.2
and Link's Awakening 1.0.0.

**The APK carries the game code library (`lasgame.so`) generated from your own game files: it is Nintendo's code,
recompiled. Never share the APK or upload it anywhere** - the same rule as `generated/` and `extracted/`.

## Order

The scripts work on a copy of the checkout at `/root/lasrecomp` (they rsync it from the Windows clone; set `SRC` /
`WORK` for other paths). The checkout must have LF line endings (`git config core.autocrlf false`).

1. `setup_wsl.sh` - host toolchain: clang 21, arm64 multiarch libraries (for the aarch64 Linux build too).
2. `install_ndk.sh` (NDK r29: clang 21, as the desktop builds), `install_sdk.sh` (platform 35, build-tools; needs
   `openjdk-17-jdk-headless`). For Pokémon Legends: Z-A's import also `dotnet-sdk-8.0` (tools/zaimport).
3. `tools/linux/build_deps.sh aarch64` once, for its pinned, checksummed source tarballs in `build/linux/src`, then
   `deps_android.sh all`: SDL3, shaderc (glslang / SPIRV-Tools linked in), LGPL FFmpeg, Vulkan + VMA headers.
4. The game code:
   - **Pokémon Legends: Z-A**: `import_za.sh <folder with the .nsp / .xci and prod.keys>` (zaimport: exefs and
     `romfs.bin` into `/root/za-extracted`; `EXCLUDE=DLC` leaves out a DLC whose ticket needs `title.keys`), then
     `build_za.sh` (generated into `generated-android-za/`, built into `build/android-za/`), then `za_apk.sh` (the
     runtime again, the project and the APK, copied to the Windows folder). After a runtime change only `za_apk.sh`.
   - **Link's Awakening**: your exefs in `extracted/exefs`, then `rebuild_relocated.sh` (`make gen PLATFORM=android`
     into `generated-android/`, a hook-count check against the desktop-base tree, then `build_gamelib_android.sh`),
     then `make_project.sh` and `build_apk.sh`.
5. `adb install -r` the APK; copy the game files to `/sdcard/Android/data/org.recomp.za/files/extracted/`: Z-A's
   `exefs/` and `romfs.bin` (6 GB: `adb push` takes about three minutes over USB 3), or Link's Awakening's `exefs/`
   and `romfs/`.

Checks: `check_build_id.py` (is a dump's build ID supported), `check_gen_rebase.py` (patch / hook sites at a base),
`verify_deps.sh`, `verify_shaderc.sh`, `check_libcxx.sh` (nothing packaged needs libc++_shared).
Device probes, built here and run with `adb shell` from `/data/local/tmp`: `test_fiber_device.sh` (the fiber switch),
`build_test_ahb.sh` (`test_ahb_gmem.c`: AHardwareBuffer import, both directions), `build_test_turnip.sh`
(`test_turnip.c`: what a custom Turnip driver build offers, loaded as a HAL module).
The aarch64 Linux build (no Android): `build_aarch64.sh`, `gen_aarch64.sh`.

## What is different on Android

- The guest window is at 0x20_0000_0000, not 0x70_0000_0000: scudo, the platform allocator, owns that range from
  the start of every app process. Everything is written as desktop addresses and moved by `GUEST_SHIFT` (runtime/rt.h),
  the address tables and the generator's patch / hook sites included (tools/game_addresses.py).
- An app cannot start a child process, so the game code is built ahead of time here, never by the start screen
  (runtime/platform/android.c `plat_spawn`). It is named through `LARECOMP_GAME_LIB`.
- Shader global memory (every storage buffer on the Switch's GPU): Qualcomm's driver has no
  `VK_EXT_external_memory_host`, so the guest window can't be imported by pointer. The top 256 MB of Z-A's physical
  memory pool - the window shaders reach on an integrated GPU - is an AHardwareBuffer instead, mapped at the guest
  address when the game maps the pool and imported by the renderer (`[share]` and `gmem:` log lines; kernel.c,
  platform/android.c `plat_gpu_share`, exec.c `ahb_import`). Without it: broken geometry, no characters, empty text.
  Never allocate an AHardwareBuffer of 1 GB or more: it is refused, and from 2 GB the vendor allocator service crashes
  and Android restarts its display stack.
- Rendering: Vulkan features are checked one by one against the driver (an Adreno lacks some a 1.3 desktop driver
  has), the swapchain is pre-rotated (the phone's native orientation is portrait), and the resolution steps down with
  Android's thermal headroom forecast (`[thermal]` log lines; `LARECOMP_THERMAL=0`: off; where the system never
  answers, the kernel's thermal zones instead). Each step is a real resolution change: drawing into part of the
  targets put Z-A's picture in a corner.
- Threads (runtime/platform/android.c): the render thread on the fastest core, the game thread on the next tier, the
  Vulkan recorder and the render thread's helpers off the fastest core (`LARECOMP_AFFINITY`, `_RECORDER`,
  `_WORKERS`); all of them named (`za-*`) for top / simpleperf.
- nn::os fibers (Z-A's job system) switch with an aarch64 context switch of their own
  (runtime/platform/android_fiber.c): bionic has no `swapcontext` on arm64.
- Touch controls are drawn over the game (runtime/ui/touch_overlay.mm): on by default on Android, hidden while a
  controller is in use, back with the next touch (`LARECOMP_TOUCH=0`: off). The menu button and Android's Back
  gesture open the settings menu.
- The free fonts the desktop ships beside the executable travel as APK assets and are unpacked to internal storage
  (`LARECOMP_RESOURCE_DIR`); Android's own Roboto lacks the glyph the game needs.
- An app has no environment to set: `<data folder>/env.txt` holds `NAME=value` lines (the `LARECOMP_*` switches)
  read at start.

The log is `lasrecomp.log` in the app's data folder above; after a crash, Android restarts the app and the crashed
run's log is `lasrecomp-backup-*-<pid>.log`. The save is the app's own file: delete it with
`adb shell run-as org.recomp.za rm -rf /storage/emulated/0/Android/data/org.recomp.za/files/save` (the debug APK).
