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
