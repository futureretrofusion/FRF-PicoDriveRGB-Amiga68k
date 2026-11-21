# PicoDriveRGB v0.2.20 — Video Modes

The **default** output remains `--output=ham6`. v0.2.20 extends the frontend with native AGA modes and accelerated RGB565 RTG integer scaling.

## Native AGA

| Argument | Depth | Description |
|---|---:|---|
| `--output=ham6` | 6 bitplanes | Original optimized HAM6; optional ARM acceleration and ScreenBuffer pipeline |
| `--output=ham8` | 8 bitplanes | AGA HAM8: 64 base colours plus 6-bit component hold-and-modify |
| `--output=aga64` | 6 bitplanes | AGA 64 indexed colours |
| `--output=aga128` | 7 bitplanes | AGA 128 indexed colours |
| `--output=aga256` | 8 bitplanes | AGA 256 indexed colours |

The new HAM8 / indexed presenters use a 68K RGB565-to-planar converter with changed-UWORD writes into CHIP RAM. They require an AGA-capable chipset. The indexed modes use fixed deterministic RGB cube palettes (64 = 2/2/2 bits, 128 = 3/2/2 bits, 256 = 3/3/2 bits) to avoid palette flicker. This is not a dynamic palette matching algorithm. The AGA mode selector excludes HAM, EHB, and dual-playfield display IDs for normal palette modes.

HAM8 and AGA indexed modes do **not** currently use the HAM6-only FRF ARM conversion ABI. `--dbuf=on` applies only to the HAM6 path. Non-AGA hardware will fail mode initialization rather than silently selecting an incompatible mode.

Example:

```text
PicoDriveRGB_v0.2.20_AGA_RTG --output=ham8 Work:ROMs/Sonic.md
PicoDriveRGB_v0.2.20_AGA_RTG --output=aga256 Work:ROMs/Sonic.md
```

## CyberGraphX RTG

| Option | Values | Action |
|---|---|---|
| `--output=rtg` | — | Select RTG |
| `--window` | — | Workbench window instead of private fullscreen screen |
| `--rtg-mode` | `native` (default), `720p` | Choose legacy scale-based geometry or 1280×720 |
| `--scale` | `1`, `2` (default), `3`, `4`, `auto` | Nearest-neighbour integer scaling (no fractional interpolation) |
| `--scanlines` | `0` (default), `25`, `50`, `75` | Darken the last row of each enlarged pixel block (at 1×, alternating rows) |

`--scale=auto` picks the largest integer scale in 1–4 that fits the *actual emulated frame* within the RTG screen, centered with black borders. With `--rtg-mode=720p`, a 320×240 frame fits at 3×, and a 320×256 frame fits at 2×. Note that `--rtg-mode=720p` selects geometry only; the graphics driver chooses refresh rate.

The direct RGB565 screen-bitmap path now scales at 1–4× and supports scanlines when the bitmap pixel format is RGB16 or RGB16PC and memory is safely accessible. Otherwise, it falls back to `WritePixelArray` RGB24. Direct writes to a Workbench screen are restricted to the frontmost window to avoid bypassing other windows' clipping.

Examples:

```text
PicoDriveRGB_v0.2.20_AGA_RTG --output=rtg --rtg-mode=720p --scale=auto --scanlines=50 Work:ROMs/Sonic.md
PicoDriveRGB_v0.2.20_AGA_RTG --output=rtg --window --scale=2 --scanlines=25 Work:ROMs/Sonic.md
```

## Keyboard and exit

The active PicoDrive window accepts cursor arrows for direction, Right Shift for B, Ctrl for A, Left Alt for C, and Enter for Start. **ESC quits** from an active RTG or native AGA/HAM Intuition screen; `Ctrl+C` remains a CLI fallback.

## Testing status

- AmigaOS m68k source syntax verified with cross-GCC; complete executable linked successfully.
- Actual HAM8, AGA palette and RTG performance require real AGA / Emu68 testing.
- Native AGA 64/128/256 and HAM8 are experimental implementations. Screen-mode availability depends on Kickstart, chipset and monitor definitions.
- No game ROMs are bundled. Read `COPYING_PICODRIVE` and `THIRD_PARTY_NOTICES.md` before redistribution.
