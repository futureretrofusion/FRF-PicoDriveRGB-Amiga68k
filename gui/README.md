# FRF PicoDriveGUI — standalone AmigaOS launcher

PicoDriveGUI is a **separate AmigaOS 3.x 68020+ native GadTools program**. It does not modify the emulator executable. It opens on Workbench, selects a Mega Drive ROM and an installed PicoDriveRGB executable, lets you configure every v0.2.21 CLI option, and starts the emulator. When the game exits, the launcher window returns.

**Development build: hardware runtime not yet verified.** The Amiga 68k cross-compile/link passes. No ROMs are distributed.

## Installation

Copy **PicoDriveGUI** and **PicoDriveRGB_v0.2.21_JOYFIX** to the same Amiga directory (or select the executable from its actual location). Run PicoDriveGUI from the CLI, or launch it from Workbench after providing a suitable icon. Use **Choose ROM** to select an .md/.bin/.gen game. Use **Choose EXE** to select the emulator if it is not found.

The GUI is paged to fit a standard 640x256 Workbench display, including non-RTG PAL. It uses GadTools and asl.library; no MUI or Reaction dependencies.

### Pages

- **Video**: HAM6, HAM8, AGA 64/128/256, RTG; RTG native/1280x720; integer scale auto/1x–4x; scanlines off/25/50/75%; windowed RTG; HAM6 double buffering.
- **Performance**: renderer accurate/good/fast; frameskip off/auto/manual; frameskip threshold 20/25/33/40/50/60; FRF ARM coprocessor on/off.
- **Input**: Mega Drive 3/6-button pad; joystick backend auto/direct/lowlevel/gameport; physical Amiga port 2/1; joystick debug.
- **Audio**: audio enabled/disabled; 44100/22050 Hz.

The ROM and emulator paths remain visible on every page. **Save prefs** writes `PicoDriveGUI.prefs` in the current working directory. **Defaults** resets settings (but retains the selected paths). **Launch game** closes the launcher window, runs PicoDriveRGB, then restores the launcher when PicoDrive exits (ESC or Ctrl+C). The launch command is logged to the CLI for troubleshooting.

The GUI does not yet offer game-library artwork, an on-screen FPS meter, or an explicit RTG 60 Hz selection because those features are not present in the v0.2.21 emulator CLI.

## Build

```sh
./gui/build.sh
```

Or build to an explicit destination:

```sh
./gui/build.sh /home/fff2025/Downloads/PicoDriveGUI_v0.1
```

Requires the AmigaOS cross compiler at `/opt/amiga/bin/m68k-amigaos-gcc` (or set `AMIGA_CC`).
