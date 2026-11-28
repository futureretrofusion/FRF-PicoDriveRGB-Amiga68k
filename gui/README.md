# FRF PicoDriveGUI — standalone AmigaOS launcher

PicoDriveGUI is a **separate AmigaOS 3.x 68020+ native GadTools program**. It does not modify the emulator executable. It opens on Workbench, selects a Mega Drive ROM and an installed PicoDriveRGB executable, lets you configure every v0.2.21 CLI option, and starts the emulator. When the game exits, the launcher window returns.

**v0.2 source candidate: not yet compiled or hardware tested.** The Studio build host is offline. The previous v0.1 binary is not this revision. No ROMs are distributed.

## Installation

Copy **PicoDriveGUI** and **PicoDriveRGB_v0.2.21_JOYFIX** to the same Amiga directory (or select the executable from its actual location). Run PicoDriveGUI from the CLI, or launch it from Workbench after providing a suitable icon. Click **SELECT ROM...** to choose an .md/.bin/.gen game, then **PLAY GAME**. Both controls are always visible. Use **Choose emulator** if the executable is not found.

The compact two-column layout uses Topaz 8 and is designed for a 640x200 or larger Workbench screen (including native PAL and RTG). It uses GadTools and asl.library; no MUI or Reaction dependencies.

### Pages

- **Video**: HAM6, HAM8, AGA 64/128/256, RTG; RTG native/1280x720; integer scale auto/1x–4x; scanlines off/25/50/75%; windowed RTG; HAM6 double buffering.
- **Performance**: renderer accurate/good/fast; frameskip off/auto/manual; frameskip threshold 20/25/33/40/50/60; FRF ARM coprocessor on/off.
- **Input**: Mega Drive 3/6-button pad; joystick backend auto/direct/lowlevel/gameport; physical Amiga port 2/1; joystick debug.
- **Audio**: audio enabled/disabled; 44100/22050 Hz.

The ROM and emulator paths remain visible on every page. **Save preferences** writes `PicoDriveGUI.prefs` beside the launcher. **Restore defaults** resets settings (but retains the selected paths). **PLAY GAME** closes the launcher window, runs PicoDriveRGB, then restores the launcher when PicoDrive exits (ESC or Ctrl+C). The launch command is logged to the CLI for troubleshooting.

The GUI does not yet offer game-library artwork, an on-screen FPS meter, or an explicit RTG 60 Hz selection because those features are not present in the v0.2.21 emulator CLI.

## Build

```sh
./gui/build.sh
```

Or build to an explicit destination:

```sh
./gui/build.sh /home/fff2025/Downloads/PicoDriveGUI_v0.2
```

Requires the AmigaOS cross compiler at `/opt/amiga/bin/m68k-amigaos-gcc` (or set `AMIGA_CC`).
