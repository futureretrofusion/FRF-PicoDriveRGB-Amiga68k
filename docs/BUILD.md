# Build

## Required environment

A working AmigaOS m68k GCC cross toolchain providing at least:

```text
m68k-amigaos-gcc
m68k-amigaos-ar
m68k-amigaos-strip
```

AmigaOS development headers/libraries must provide the Exec, Intuition, Graphics, lowlevel, gameport and AHI interfaces used by the host.

## Reproducible base

Use PicoDrive commit:

```text
26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

The existing helper can fetch the pinned tree and recursively initialize PicoDrive's submodules:

```bash
cd frf-port
./FETCH_AND_BUILD_MINI.sh
```

Or point the builder at an existing checkout:

```bash
cd frf-port
./BUILD_PICODRIVE_AMIGA68K_MINI.sh /path/to/picodrive
```

The expected executable name is:

```text
FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX
```

## Validation before publishing a binary

Run the supplied equivalence and structural validators, build with the target m68k compiler, test on real Amiga/PiStorm hardware, and record the SHA-256 of the exact executable. A public binary should be released only from a repository state that includes the complete corresponding modified PicoDrive source as required by PicoDrive's redistribution terms.
