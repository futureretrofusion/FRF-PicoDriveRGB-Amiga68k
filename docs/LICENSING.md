# Licensing and redistribution

## PicoDrive

This project is a modified/derivative PicoDrive distribution. The authoritative licence is the `COPYING` file in the PicoDrive source tree. The public repository must retain that file unchanged.

Important distribution conditions in PicoDrive's licence include:

- redistribution is non-commercial
- modified redistribution must include complete corresponding source code for all components used by the binary, subject to the operating-system/toolchain exception stated in `COPYING`
- copyright, conditions and disclaimer must be reproduced with distributions

For that reason, the preferred Future Retro Fusion public repository is a **public fork/full source tree of the pinned PicoDrive revision**, not a binary-only repository and not merely a patch archive.

## Future Retro Fusion changes

Future Retro Fusion's PicoDrive-specific modifications are published as part of the derivative source tree under the PicoDrive redistribution terms where those terms apply. This project does not add a commercial-use permission that PicoDrive itself does not grant.

## libretro API/helper code

The PicoDrive tree contains libretro API/header/helper code with its own permissive notices. Those file-level notices must remain intact. In particular, the libretro API header and the `compat_strcasestr.c` helper used by this host carry their own permissive licence notices in the upstream source.

## AmigaOS / AHI / toolchain

The repository does not redistribute the AmigaOS NDK, AHI SDK, clib2, GCC toolchain, or operating-system libraries. They are build/runtime dependencies supplied separately under their own terms.

## ROMs and BIOS

No ROMs, BIOS images, or game assets are distributed by Future Retro Fusion as part of this port.

## Release rule

Do not attach a compiled PicoDriveRGB binary to a public release unless the exact source used to build it is present in the public repository/tag and all upstream/file-level notices are preserved.

This document is a project compliance note, not legal advice; the upstream `COPYING` and file-level notices are authoritative.
