# Porting notes

## PicoDrive base

This port is based on notaz PicoDrive commit:

```text
26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

The build uses the libretro core as a static archive and a small native Amiga host.

Key target choices:

```text
platform=amiga68k
use_libchdr=0
use_sh2drc=0
use_cyclone=0
use_drz80=0
PLATFORM_ZLIB=1
USE_LIBRETRO_VFS=0
```

This deliberately favours portable C components suitable for the m68k Amiga build.

## Core modifications

The FRF build patch adds an `amiga68k` target to `Makefile.libretro`, provides Amiga-safe memory mapping helpers, removes unused Sega Pico overlay dependencies from the Mega Drive proof target, and supplies a minimal monotonic tick fallback for code paths that otherwise require `gettimeofday()`.

The native host registers PicoDrive's libretro callbacks directly, then owns Amiga-specific video, audio, input, timer, and optional CPU3 transport.

## FRF host modules

```text
frf_pd_libretro_mini.c  native host / pacing / CLI / libretro callbacks
frf_pd_ham6.c           native HAM6 custom screen and CHIP presenter
frf_pd_audio.c          AHI worker, ring, linked double buffer
frf_pd_input.c          lowlevel + gameport fallback
frf_pd_timer.c          absolute-deadline host pacing
frf_pd_coproc_client.c  optional FRF Emu68 CPU3 client
frf_coproc_abi.h        shared ABI contract
```

## Native video principle

The port is not an RTG wrapper. PicoDrive's RGB565 output is transformed into a native HAM6 planar representation and written to CHIP RAM for Denise to display through the physical Amiga RGB output.
