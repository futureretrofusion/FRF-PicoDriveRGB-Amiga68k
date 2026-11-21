# Direct CHIP native RGB/HAM6 architecture

## Purpose

Classic Amiga Denise displays planar CHIP-RAM bitplanes. PicoDrive's libretro core produces chunky RGB565. Future Retro Fusion's Direct CHIP system bridges those two representations without routing the final picture through RTG.

## Default single-buffer path

```text
Mega Drive / Genesis ROM
        ↓
PicoDrive core
        ↓
RGB565 frame in normal/Fast RAM
        ↓
CPU3 HAM6 packer (when FRF Emu68 coprocessor is present)
        or exact 68K HAM6 encoder
        ↓
six packed HAM6 planes in normal/shared RAM
        ↓
68K planar shadow comparison
        ↓
dirty UWORD CHIP-RAM runs only
        ↓
visible 320x256 HAM6 custom-screen bitmap
        ↓
Denise → physical Amiga RGB
```

The 68K host retains ownership of the Amiga custom chipset and the actual CHIP writes. The ARM worker never directly drives Denise or steals display ownership.

## Why HAM6

RGB565 is not a native Denise framebuffer format. HAM6 provides a native six-bitplane method of representing a much wider colour range while remaining a real custom-chip display mode.

Each scanline starts with a known held colour state. The encoder evaluates a direct palette code and HAM modify-red/green/blue candidates with deterministic tie ordering. A hardware equivalence gate compares the CPU3 result with the exact 68K implementation before ARM acceleration is accepted.

## Dirty writes

The presenter keeps a planar shadow in non-CHIP memory. It compares packed words and copies only changed runs into CHIP RAM. This reduces bus pressure compared with copying all six planes every frame.

## Optional true ScreenBuffer double buffering

`--dbuf=on` allocates two native CHIP ScreenBuffers. Rendering occurs only into a buffer Intuition has reported safe, followed by `ChangeScreenBuffer()` when ownership permits.

A two-page display cannot use only "changed since previous emulated frame" metadata, because the back page may be two or more presentations old. The FRF path therefore tracks a shadow/generation state per CHIP page and brings each safe back page forward to the newest completed packed frame before flipping it.

V0.2.16C also handles PicoDrive geometry changes (for example 256↔320 width or 224↔240 height) without abandoning a hidden page: an old-layout CPU3 job is drained/discarded asynchronously, then both CHIP pages are force-refreshed for the new geometry.

## 60 Hz core versus PAL 50 Hz output

When a native NTSC HAM6 mode is unavailable, the Mega Drive core may still run at 60 Hz while the Amiga display presents at 50 Hz. The host uses deterministic presentation-rate dropping. This is separate from PicoDrive's generic core frameskip; `--frameskip=off` remains the normal FRF configuration.
