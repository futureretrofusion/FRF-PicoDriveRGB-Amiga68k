# Changelog

## v0.2.21 — digital joystick polling fix (development)

- Read JOY1DAT / CIAA PRA directly for standard digital Amiga joystick 2; optional physical connector 1
- Merge the direct source with lowlevel.library and gameport.device, preventing controller claim failures from disabling standard joystick input
- Fix gameport absolute-position handling when events include fire-button transitions
- Add `--joyinput=auto|direct|lowlevel|gameport`, `--joyport=1|2`, and `--joydebug`
- Add host regression for digital joystick direction/fire decoder; cross-compiled new Amiga 68k executable
- Runtime validation with real Amiga/Emu68 joystick is still outstanding


## v0.2.16C — public candidate

- optional true native HAM6 ScreenBuffer page flipping
- per-page shadow/generation correctness for double-buffered dirty updates
- geometry-transition repair for games switching 256/320 width or 224/240 height
- input acquisition ordered before HAM6 custom-screen initialization
- preserves V0.2.15 direct-front presenter as default (`--dbuf=off`)
- preserves asynchronous CPU3 HAM6 pipeline
- preserves 44.1 kHz linked AHI double buffering and interleaved audio governor
- public CLI identity expanded to **Future Retro Fusion PicoDriveRGB**
- added `--help` and `--version`
