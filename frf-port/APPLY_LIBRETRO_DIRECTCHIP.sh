#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-}"
[[ -n "$ROOT" && -f "$ROOT/platform/libretro/libretro.c" && -f "$ROOT/Makefile.libretro" ]] || {
  echo "Usage: $0 /path/to/picodrive" >&2; exit 2;
}
SELF="$(cd "$(dirname "$0")" && pwd)"
DST="$ROOT/platform/libretro"
MARK=FRF_PICODRIVE_DIRECTCHIP_V0_1

cp "$SELF/src/frf_pd_ham6.c" "$DST/frf_pd_ham6.c"
cp "$SELF/src/frf_pd_ham6.h" "$DST/frf_pd_ham6.h"

python3 - "$ROOT/platform/libretro/libretro.c" "$ROOT/Makefile.libretro" <<'PY'
from pathlib import Path
import sys,re
c=Path(sys.argv[1]); m=Path(sys.argv[2])
s=c.read_text(errors='strict'); mk=m.read_text(errors='strict')
mark='FRF_PICODRIVE_DIRECTCHIP_V0_1'
if mark not in s:
    # Include alongside local libretro headers.
    needle='#include <libretro.h>'
    if needle not in s: raise SystemExit('libretro include anchor not found')
    s=s.replace(needle, needle+'\n#include "frf_pd_ham6.h" /* '+mark+' */',1)

    # Current PicoDrive emits the completed RGB565 frame here.
    needle='video_cb((short *)buff, vout_width, vout_height, vout_width * 2);'
    if needle not in s: raise SystemExit('final video_cb anchor not found')
    repl='''/* FRF_PICODRIVE_DIRECTCHIP_V0_1\n    * If native HAM6 consumes the RGB565 frame, bypass the frontend video copy.\n    * Input/audio/timing remain owned by the libretro frontend. */\n   if (!frf_pd_ham6_present_rgb565(buff, (unsigned)vout_width,\n          (unsigned)vout_height, (unsigned)vout_width * 2U))\n      video_cb((short *)buff, vout_width, vout_height, vout_width * 2);'''
    s=s.replace(needle,repl,1)

    # Close the native custom screen when the core is unloaded/deinitialised.
    mm=re.search(r'void\s+retro_deinit\s*\(\s*void\s*\)\s*\{',s)
    if not mm: raise SystemExit('retro_deinit anchor not found')
    pos=mm.end()
    s=s[:pos]+'\n   frf_pd_ham6_shutdown(); /* '+mark+' */'+s[pos:]
    c.write_text(s)
else:
    print('libretro.c already patched')

obj='platform/libretro/frf_pd_ham6.o'
if obj not in mk:
    # Put it before the top-level Makefile include so it is part of every libretro target.
    needle='include Makefile'
    if needle not in mk: raise SystemExit('Makefile.libretro include anchor not found')
    mk=mk.replace(needle,'OBJS += '+obj+'\n\n'+needle,1)
    m.write_text(mk)
else:
    print('Makefile.libretro already patched')
PY

echo "Patched PicoDrive libretro tree: $ROOT"
echo "Runtime selection:"
echo "  FRF_PD_VIDEO=ham6   direct CHIP/HAM6 -> native physical RGB"
echo "  FRF_PD_VIDEO unset  original frontend RGB path"
