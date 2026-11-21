# Future Retro Fusion PicoDriveRGB for AmigaOS 68k

**Future Retro Fusion PicoDriveRGB** is an AmigaOS 68k port/front-end for the PicoDrive Mega Drive / Genesis emulator, with native AGA/HAM chipset video and CyberGraphX RTG output.

Current public candidate: **v0.2.20 development (cross-compiled; hardware testing pending)**
PicoDrive base: **notaz/picodrive commit `26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee`**

> Future Retro Fusion project: native RGB/HAM6 presentation, Direct CHIP updates, AHI audio transport, classic Amiga input, and an optional client for the FRF Emu68 ARM coprocessor ABI.

## v0.2.20 AGA and RTG video expansion

**Development build (cross-compiled; hardware verification pending).** New output selections: `--output=ham8`, `--output=aga64`, `--output=aga128`, `--output=aga256`, plus existing `--output=ham6` (default) and `--output=rtg`. RTG now supports `--rtg-mode=720p`, `--scale=auto` and 16-bit direct integer scaling/scanlines. **ESC exits** from the active RTG or AGA/HAM screen; Ctrl+C from the CLI still works. See [Video Modes](docs/VIDEO_MODES.md) for examples, colour limits and testing status.

```text
PicoDriveRGB_v0.2.20_AGA_RTG --output=ham8 Work:ROMs/Sonic.md
PicoDriveRGB_v0.2.20_AGA_RTG --output=aga256 Work:ROMs/Sonic.md
PicoDriveRGB_v0.2.20_AGA_RTG --output=rtg --rtg-mode=720p --scale=auto --scanlines=50 Work:ROMs/Sonic.md
```

## What makes this port different

PicoDrive normally produces a chunky RGB frame. Classic Amiga Denise does not consume RGB565 directly, so this port turns PicoDrive's RGB565 output into a native **HAM6 six-bitplane CHIP-RAM screen**. The default presenter writes only changed UWORD runs into the displayed CHIP bitmap. An optional `--dbuf=on` path uses two native ScreenBuffers and page flips when the back page is safe.

On FRF-modified Emu68 firmware, an optional ARM CPU3 worker can perform the expensive RGB565 → HAM6 decision/packing stage asynchronously. The 68K host still owns the Amiga display and CHIP writes. If the FRF coprocessor ABI is absent or fails validation, the port falls back to the exact 68K HAM6 encoder.

Audio is independent of the video pipeline: AHI runs at 44.1 kHz using linked double-buffered requests, with an interleaved audio-protection governor that sheds presentation work rather than dropping game audio.

## Current defaults

```text
--frfcp=on
--dbuf=off
--renderer=accurate
--audio-rate=44100
--frameskip=off
--pad=3
sound=on
```

`--dbuf=off` is intentionally the safe/default presenter. `--dbuf=on` enables the optional native ScreenBuffer page-flip path.

## CLI

```text
FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX --help
FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX --version
```

Typical launch:

```text
FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX "Work:roms/Sonic The Hedgehog (USA, Europe).md"
```

Page-flip test:

```text
FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX --dbuf=on "Work:roms/game.md"
```

## Hardware/software target

- AmigaOS 68k
- PiStorm / Emu68 is the primary accelerated target
- native Amiga custom-screen output through Denise RGB
- HAM6, 320-wide presentation surface
- AHI for audio
- lowlevel.library controller input with classic `gameport.device` fallback

The ARM coprocessor is optional. A normal 68K fallback remains part of the same executable.

## Documentation

- [`docs/PORTING.md`](docs/PORTING.md) — port structure and PicoDrive integration
- [`docs/DIRECT_CHIP.md`](docs/DIRECT_CHIP.md) — native HAM6 / CHIP-RAM pipeline
- [`docs/EMU68_ARM_COPROCESSOR.md`](docs/EMU68_ARM_COPROCESSOR.md) — FRF CPU3 ABI and future firmware publication plan
- [`docs/AUDIO.md`](docs/AUDIO.md) — AHI double buffering and audio governor
- [`docs/INPUT.md`](docs/INPUT.md) — Amiga controller routing
- [`docs/BUILD.md`](docs/BUILD.md) — reproducible build path
- [`docs/VIDEO_MODES.md`](docs/VIDEO_MODES.md) — HAM8, AGA indexed, RTG integer scaling and keyboard exit
- [`docs/LICENSING.md`](docs/LICENSING.md) — redistribution and complete-source requirements

## ROMs

No game ROMs, BIOS images, or copyrighted game assets are included. Users must provide their own legally obtained software images.

## Credits

PicoDrive is the work of notaz and its contributors. The AmigaOS 68k native RGB/HAM6 port, Direct CHIP systems, AHI transport work, and FRF coprocessor integration are developed under the **Future Retro Fusion** project.


## RTG v0.2.19 keyboard update (21 November 2026)

The current frf-port/src/ sources include the v0.2.19 development changes: fullscreen 1x RGB565 fast path where compatible and RTG Intuition RAWKEY input. Keyboard mapping: cursor arrows = movement, Right Shift = Mega Drive B, Ctrl = Mega Drive A, Left Alt = Mega Drive C, Return = Start. Joystick/CD32 mappings remain available. The keyboard input system now includes a native HAM/AGA screen handler in v0.2.20. Cross-compile and link succeeded; runtime behavior on real Emu68/Amiga hardware is not yet verified.

Build the current code using frf-port/BUILD_PICODRIVE_DUAL_V0_2_17.sh with the upstream PicoDrive source. Upstream source is a separate build dependency. Read COPYING_PICODRIVE and THIRD_PARTY_NOTICES.md before distributing modified builds; the upstream license includes non-commercial restrictions and a corresponding-source requirement.
