<div align="center">

<img src="docs/icon/icon_1024.png" alt="ZA Recomp" width="128">

# PokeZ-Android

**An Android port of ZA Recomp: *Pokémon Legends: Z-A* (Nintendo Switch 1 version) running natively on an arm64
phone, built by static recompilation.**

**English** · [Português (Brasil)](README.pt-BR.md)

[Report a bug](https://github.com/MrSw4gGuy/PokeZ-Android/issues) ·
[ZA Recomp for PC](https://github.com/Hexveil404/PokeZ-ARecomp) ·
[Build guide](tools/linux/android-port/README.md)

</div>

---

> [!WARNING]
> **Work in progress.** The game plays from a new save through the intro and into Lumiose City on the test phone, with
> visual bugs left (see [Status](#status)). It has been tested on one phone only.

This branch (`android-port`) takes [ZA Recomp](https://github.com/Hexveil404/PokeZ-ARecomp) - the game's Switch code
translated ahead of time into C, with the Switch's operating system and GPU (NVN) reimplemented on top of Vulkan - and
builds it for Android on arm64. The game's ARM64 code becomes native arm64 code again (NEON instructions kept as they
are), so no emulator and no instruction translation run while you play.

It is built on **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by Peiikai and **ZA Recomp** by Hexveil404.

> [!IMPORTANT]
> **You need your own copy of the game, and you build the APK yourself.** No game files, code, assets or fonts are
> included. The APK you build contains the game's code recompiled from *your* dump, so there is no ready-made APK to
> download and you must never share yours.

## Contents

- [Status](#status)
- [Requirements](#requirements)
- [Building and installing](#building-and-installing)
- [Controls](#controls) ([Image quality](#image-quality))
- [Saves](#saves)
- [What is different from the PC version](#what-is-different-from-the-pc-version)
- [Troubleshooting](#troubleshooting)
- [Credits](#credits)
- [Legal](#legal)

## Status

Tested on a **Samsung Galaxy S25+** (Snapdragon 8 Elite, Adreno 830, 12 GB RAM, Android 17, Qualcomm's own Vulkan
driver) with Pokémon Legends: Z-A **2.0.2**, from a new save.

| | |
|---|---|
| Start screen, game setup check | Works |
| Language selection, save creation, character creation, name entry | Works |
| Intro cutscenes, train, Lumiose City | Works: no heavy visual bugs left (the eyes below), the game's own pop-in |
| Frame rate | A steady 30 fps (the game's own rate on the Switch) in the scenes tested so far. 60 fps: about 53 at 75% resolution (the GPU is the limit); **Dynamic 30 / 60 fps** (Settings > Video) goes to 30 instead of slow motion and back to 60 when there is room - not tested on the phone yet |
| Text and dialogue boxes | Works |
| Controller (tested: 8BitDo Pro 3, Bluetooth) | Works |
| Touch controls | Works (D-pad tested in the game's menus; the rest in progress) |
| Saving | Works |
| Memory | About 3 GB in use |
| Heat | The resolution steps down by itself as the phone warms (thermal headroom): 75% of the screen, down to about 45% once hot |
| Upscaling | Snapdragon Game Super Resolution (Qualcomm's own upscaler) by default; AMD FSR 1 and bilinear in the menu |
| *Mega Dimension* DLC | Not tested yet |

**Known visual bugs** (being worked on):
- Some eyes look wrong: the iris drawn as blue and white squares, a dark rectangle behind some Pokémon's eyes.
- Faint straight light strips over faces and bodies in some close-up cutscenes (seen in one run).
- The picture is soft once the phone is hot: the resolution goes down to keep the temperature in check (see
  [Image quality](#image-quality)).

## Requirements

| | |
|---|---|
| **Phone** | Android 12 or newer, arm64 (64-bit), Vulkan 1.3. Tested: Snapdragon 8 Elite (Adreno 830) only |
| **RAM** | 8 GB at least; tested with 12 GB |
| **Storage** | About 7 GB free on the phone (the game data is 6 GB, the APK about 500 MB) |
| **Game** | Pokémon Legends: Z-A **2.0.2** (Switch 1 version): the base game and the update, with the `prod.keys` from your own Switch |
| **To build** | A PC with Windows 10/11 and WSL2 (Ubuntu 22.04), or any x86-64 Linux; about 30 GB free; a USB cable and [platform-tools](https://developer.android.com/tools/releases/platform-tools) (`adb`) |

## Building and installing

The whole build runs on your PC, from scripts in [`tools/linux/android-port/`](tools/linux/android-port/README.md);
that guide has each step and option. In short:

1. Set up the toolchain once: `setup_wsl.sh`, `install_ndk.sh`, `install_sdk.sh`, then the libraries with
   `deps_android.sh all`.
2. Import your game: put your base game and update (`.nsp` / `.xci`) and `prod.keys` in one folder and run
   `import_za.sh <that folder>`.
3. Build the game code and the APK: `build_za.sh`, then `za_apk.sh`. The game code takes a while the first time.
4. Turn on USB debugging on the phone, then install the APK and copy the game files:

   ```bash
   adb install -r ZARecomp-za-debug.apk
   adb push za-extracted/exefs /sdcard/Android/data/org.recomp.za/files/extracted/exefs
   adb push za-extracted/romfs.bin /sdcard/Android/data/org.recomp.za/files/extracted/romfs.bin
   ```

5. Open **ZA Recomp** on the phone and tap **Play**.

## Controls

- **Controller**: any controller Android sees (Bluetooth or USB). It is picked up on its own.
- **Touch**: on-screen buttons drawn over the game - A/B/X/Y, the D-pad, L/R/ZL/ZR, + and −, a left stick that appears
  where your thumb lands, and a drag on the right half of the screen for the camera. They hide while a controller is in
  use and come back with the next touch.
- **Settings menu**: the ☰ button at the top of the screen, or Android's Back gesture.
- **Layout**: the Switch's by default - the right button is A (confirm), the bottom one B (back), in the game and in
  the menus, with Nintendo button icons. **Settings > Controls > A/B/X/Y layout > Xbox** puts A at the bottom instead.

### Image quality

The game renders below the screen's resolution and is scaled up: 75% of the screen to begin with, and a step lower each
time the phone warms up (down to about 45%). In **Settings > Video**:
- **Upscaler**: *Snapdragon GSR* (the default: Qualcomm's own, made for Adreno), *AMD FSR 1*, or *Bilinear* (softest).
- **Internal resolution**: *Match display* (100%) for a sharper picture, at the cost of more heat (and so more steps
  down later).
- **Anti-aliasing**: *SMAA* smooths jagged edges, at a small cost.

## Saves

Saves are the game's own files, kept in `Android/data/org.recomp.za/files/save/save_0/` on the phone. They are the same
files as on the Switch and on the PC version, so they move between them without conversion (JKSV on the Switch).

## What is different from the PC version

- The game code is compiled on your PC ahead of time, not by the start screen: an Android app can't run the compiler.
- Qualcomm's driver can't share the game's memory with the GPU the way PC drivers do, so part of it is shared through
  an Android hardware buffer instead. This is what makes the 3D, the characters and the text appear.
- The picture is rotated by the renderer itself (the phone's native orientation is portrait), and the resolution goes
  down a step when the phone gets hot.
- Technical details: [tools/linux/android-port/README.md](tools/linux/android-port/README.md#what-is-different-on-android).

## Troubleshooting

- **Log**: `Android/data/org.recomp.za/files/lasrecomp.log` on the phone (after a crash, the crashed run's log is
  `lasrecomp-backup-*.log`). Please attach it to any bug report, with your phone model and Android version.
- **Debug switches**: the renderer's options (`LARECOMP_*`) go in `Android/data/org.recomp.za/files/env.txt`, one
  `NAME=value` per line, read at start.

## Credits

- **[Peiikai](https://github.com/peiikai)**: creator of [LAS Recomp](https://github.com/peiikai/LASRecomp), the static
  recompiler, runtime, renderers and shader translator this port is built on.
- **[Hexveil404](https://github.com/Hexveil404)**: [ZA Recomp](https://github.com/Hexveil404/PokeZ-ARecomp), the
  Pokémon Legends: Z-A port, and its contributors.
- **[MrSw4gGuy](https://github.com/MrSw4gGuy)**: the Android port and its testing.
- **LAS Recomp contributors**: see [docs/LASRecomp-README.md](docs/LASRecomp-README.md#credits).
- **[Ryujinx](https://github.com/Ryujinx/Ryujinx)**, used as a behavioural reference (no code taken),
  **[envytools](https://github.com/envytools/envytools)** and **[Switchbrew](https://switchbrew.org)** for GPU and
  system documentation.
- **Libraries**: [SDL 3](https://libsdl.org), [FFmpeg](https://ffmpeg.org), [Dear ImGui](https://github.com/ocornut/imgui),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
  [shaderc](https://github.com/google/shaderc), [LibHac](https://github.com/Thealexbarney/LibHac) and the others listed
  in [docs/LASRecomp-README.md](docs/LASRecomp-README.md#license). Fonts bundled in the APK: BIZ UDPGothic (SIL OFL)
  and DejaVu Sans.
- **[Snapdragon Game Super Resolution](https://github.com/SnapdragonGameStudios/snapdragon-gsr)** by Qualcomm
  Innovation Center (BSD-3-Clause, [third_party/sgsr](third_party/sgsr/LICENSE)): the default upscaler.

## Legal

PokeZ-Android is a fan project. It is not affiliated with, endorsed or sponsored by Nintendo, Game Freak, Creatures or
The Pokémon Company. *Pokémon* and *Pokémon Legends: Z-A* are trademarks of their respective owners. No game files,
code, assets or fonts are included; you must provide your own legally obtained copy of the game, and the APK built from
it is for your own device only.

The code is released under the [MIT License](LICENSE) (LAS Recomp © Peiikai). Third-party components keep their own
licenses; see [docs/LASRecomp-README.md](docs/LASRecomp-README.md#license).
