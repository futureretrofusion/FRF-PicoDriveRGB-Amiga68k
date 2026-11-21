FRF PicoDriveRGB Emu68 V0.2.16C - OPTIONAL HAM6 PAGE-FLIP DOUBLE BUFFER
=====================================================================

Baseline
--------
V0.2.15 is the working default: CPU3 HAM6 is pipelined, AHI 44.1 kHz uses the
gapless double-buffer worker, and the interleaved audio-master governor keeps
music continuous without long video recovery freezes.

V0.2.16 changes only native HAM6 presentation plus the CLI/build identity.
No Emu68 change is required.

Default behaviour
-----------------
--dbuf=off is the default and retains the V0.2.15 direct-front presenter.

Experimental true page flip
---------------------------
Use --dbuf=on with the proven CPU3 HAM6 path.

The native custom screen is wrapped as ScreenBuffer 0 (SB_SCREEN_BITMAP) and a
second native CHIP ScreenBuffer is allocated as buffer 1. PicoDrive writes only
the buffer Intuition has reported safe, then calls ChangeScreenBuffer().

Ownership is intentionally conservative:

  SafeMessage for buffer N -> previous buffer N^1 becomes safe to redraw
  DispMessage for buffer N -> buffer N has actually reached display
  back must be safe AND current front must be displayed before writing/flipping
  WaitBlit() precedes ChangeScreenBuffer()

This follows the already-proven PocketSNESRGB ScreenBuffer ownership state
machine rather than inventing another interpretation.

Why a separate shadow is required
---------------------------------
ARM opcode 6 reports rows changed against the immediately previous RGB565
frame. In a two-buffer display, the back bitmap is usually two presentations
old, so those dirty bits cannot be used directly.

V0.2.16 therefore keeps one planar shadow per CHIP ScreenBuffer and a tiny
per-row generation table. CPU3's packed output is persistent in shared RAM:
rows not re-encoded retain their current packed value. Each completed CPU3 job
updates row generations from the ARM dirty bytes. When a back buffer is safe,
only rows whose generation is newer than that buffer are compared/copied into
that buffer. This makes the back bitmap exact without full-frame CHIP copies.

Fallbacks
---------
If --dbuf=on cannot allocate the ScreenBuffer pair, message ports, compatible
CHIP bitmap layout, or second FAST/Public shadow, startup logs the reason and
continues with the proven V0.2.15 single-buffer presenter.

If CPU3 HAM6 is unavailable, --dbuf=on is ignored and the existing 68K path is
retained. If CPU3 fails after page flipping started, V0.2.16 first restores
ownership to SB_SCREEN_BITMAP before returning to the existing 68K presenter.

Run
---
Experimental page flip:

  FRF_PicoDriveRGB_Amiga68k_V0_2_16_CPU3_HAM6_PAGEFLIP_DBUF \
    --frfcp=on --dbuf=on --renderer=accurate --audio-rate=44100 \
    --frameskip=off "roms/Sonic The Hedgehog (USA, Europe).md"

Exact V0.2.15-style control:

  FRF_PicoDriveRGB_Amiga68k_V0_2_16_CPU3_HAM6_PAGEFLIP_DBUF \
    --frfcp=on --dbuf=off --renderer=accurate --audio-rate=44100 \
    --frameskip=off "roms/Sonic The Hedgehog (USA, Europe).md"

Expected page-flip markers
--------------------------

  [FRF PD DBUF] ACTIVE true ScreenBuffer page flip ...
  [FRF PD HAM6] ... path=CPU3-DBUF ... dbuf_flips=...

If allocation is not possible, expect a [FRF PD DBUF] fallback message followed
by the normal CPU3-PIPE path.

What to compare
---------------
- visual tearing / partial-update shimmer
- smoothness versus --dbuf=off
- audio dropped/silence (should remain 0 or near 0)
- dbuf_flips (should rise steadily)
- dbuf_busy and dbuf_fail (should stay low; dbuf_fail ideally 0)
- chipbytes/avg and late= versus the V0.2.15-style control

Do not assume double buffering is faster. It is an optional visual-output test.


## V0.2.16C loading-screen/page-flip repair

Some Mega Drive games switch 256/320-pixel or 224/240-line geometry around
title/loading transitions. V0.2.16B could hit that transition while the
previous CPU3 frame was still pending, disable CPU3, then continue 68K drawing
into the hidden DBUF back page without flipping it. The game and audio kept
running while the visible loading image appeared frozen.

V0.2.16C keeps the transition asynchronous: an old-layout CPU3 result is
allowed to finish and is discarded, then the new geometry is submitted. Both
CHIP pages are force-refreshed once after the geometry reset. No Emu68 change
is required.

## V0.2.16B input-order repair

`src/frf_pd_input.c` remains byte-identical to V0.2.15/V0.2.16. The optional
ScreenBuffer pair is now allocated lazily on the first video callback, after
`frf_pd_input_init()` has opened/claimed the controller path. This preserves the
working screen-first base allocation while restoring V0.2.15 input resource order.

Use `--dbuf=off` as the page-flip A/B control.
