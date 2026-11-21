# Amiga input routing

The public candidate initializes controller ownership before opening the native HAM6 custom screen.

Primary path:

- `lowlevel.library` / `ReadJoyPort()`
- one detected controller is routed to Player 1 regardless of which lowlevel port index reports it
- CD32-style colour buttons map to Mega Drive controls

Classic fallback:

- `gameport.device`, unit 1 (physical Amiga joystick port 2)
- claimed as `GPCT_ABSJOYSTICK`
- asynchronous `GPD_READEVENT`
- directions plus standard fire and second-fire handling

The fallback is intentionally independent of the optional ScreenBuffer presenter so `--dbuf=off` and `--dbuf=on` share the same controller backend.

## Keyboard input (v0.2.20)

Both CyberGraphX RTG and native HAM6 / HAM8 / AGA screens now accept Intuition RAWKEY input from their active screen window: cursor keys for direction, Right Shift for B, Ctrl for A, Left Alt for C and Return for Start. The keyboard and joystick states are combined for Player 1. Key releases are tracked, and held keys are cleared when focus is lost.

**ESC quits the emulator** when its screen/window has keyboard focus. Ctrl+C from the CLI remains a fallback. The native screen event window must initialise successfully for keyboard input in HAM/AGA modes; check the `[FRF KEYS]` startup message. This functionality requires validation on Amiga hardware.

## v0.2.21 direct joystick path and troubleshooting

Direct hardware polling uses JOY1DAT ($DFF00C) for the standard joystick in physical port 2, and CIA-A PRA ($BFE001) bit 7 (active low) for its fire button. Physical port 1 uses JOY0DAT ($DFF00A) and PRA bit 6. This path requires no lowlevel.library detection and does not claim gameport.device. Joystick directions and fire are converted into libretro RetroPad states; fire maps to Mega Drive B.

The default `--joyinput=auto` merges direct CHIP/CIA polling with lowlevel.library (including CD32 buttons) and with the gameport.device fallback if it can claim the port. `gameport.device` can legitimately be occupied by another task; this no longer disables physical joystick input in automatic mode. The gameport event parser now reads absolute-joystick direction coordinates from fire-button events as well as no-button events.

Diagnostic switches: `--joyinput=direct`, `--joyinput=lowlevel`, or `--joyinput=gameport` isolate the corresponding backend; `--joyport=1|2` selects the physical connector for direct polling (default 2); `--joydebug` prints raw joystick/CIA and OS readings when they change. The log is printed to the Amiga CLI and can be redirected to a file if the application opens a fullscreen screen.

Test with a standard joystick on port 2:

```text
PicoDriveRGB_v0.2.21_JOYFIX --output=rtg --joyinput=direct --joyport=2 --joydebug Work:ROMs/Sonic.md
```

Host-side register decoder tests cover all directions, diagonals, neutral and fire on both physical ports. Those tests do not replace testing with an actual joystick attached to the Amiga / Emu68 hardware.
