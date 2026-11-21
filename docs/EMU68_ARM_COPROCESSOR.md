# Future Retro Fusion Emu68 ARM coprocessor integration

## Status

PicoDriveRGB contains an **optional client** for the Future Retro Fusion Emu68 coprocessor ABI. The modified Emu68 firmware itself is a separate project and is intended to be published independently with its own source, build instructions, licence/provenance documentation, ABI specification, and hardware validation notes.

The PicoDrive executable remains functional without that firmware by falling back to its exact 68K HAM6 implementation.

## Architecture

On the FRF Emu68/PiStorm build:

```text
CPU0: Emu68 main/JIT work
CPU1: optional logging role
CPU2: PiStorm housekeeping
CPU3: dedicated FRF ARM coprocessor worker
```

CPU3 is not exposed to AmigaOS as an SMP CPU. Amiga-side 68K software communicates through a fixed discovery block, shared control/queue structures, a bounded shared workspace, and a doorbell.

## ABI used by PicoDriveRGB

The client currently targets ABI version 1. Relevant capability:

```text
feature bit 9 : RGB565_TO_HAM6_PLANAR
opcode 6      : RGB565_TO_HAM6_PLANAR
```

The job supplies a tightly packed RGB565 frame, an output area for six packed HAM6 planes, a previous-frame area, and a 65536-entry direct-pen LUT. The worker reports changed source rows and rows processed.

## Safety model

- discovery and ABI version are checked before use
- the ARM worker must pass a real shared-memory self-test
- HAM6 acceleration is accepted only after a deterministic CPU3-vs-68K equivalence test
- timeouts, unsupported firmware, geometry-transition errors, or terminal job errors fall back to the normal 68K path
- DirectAudio/Paula, RTG, SD, and other PiStorm responsibilities are not part of the PicoDrive opcode

## Pipeline

Gameplay uses one-frame overlap:

```text
callback N:
  consume completed CPU3 frame N-1
  publish safe HAM6 data to CHIP
  copy/submit RGB565 frame N
  return

PicoDrive emulates the next frame while CPU3 works.
```

This avoids the older synchronous design where the 68K front end spun until CPU3 finished.

## Future public Emu68/ARM repository

When the FRF-modified Emu68 firmware is published, its documentation should include:

- exact upstream Emu68 base revision
- every FRF-modified firmware source file
- `frf_coproc_abi.h` as the shared contract
- CPU/core ownership map
- discovery addresses and workspace bounds
- opcode catalogue and compatibility rules
- memory-ordering / `dsb` / `sev` semantics
- build and install instructions
- hardware self-test instructions
- licence/provenance notices for Emu68 and all FRF changes

PicoDriveRGB should link to that repository rather than embedding firmware binaries in this source tree.
