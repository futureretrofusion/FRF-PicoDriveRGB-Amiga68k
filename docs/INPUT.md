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
