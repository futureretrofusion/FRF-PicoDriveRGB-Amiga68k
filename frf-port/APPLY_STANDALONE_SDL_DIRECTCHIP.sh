#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-}"
[[ -n "$ROOT" && -f "$ROOT/platform/common/plat_sdl.c" && -f "$ROOT/Makefile" ]] || {
  echo "Usage: $0 /path/to/picodrive" >&2; exit 2;
}
SELF="$(cd "$(dirname "$0")" && pwd)"
DST="$ROOT/platform/common"
cp "$SELF/src/frf_pd_ham6.c" "$DST/frf_pd_ham6.c"
cp "$SELF/src/frf_pd_ham6.h" "$DST/frf_pd_ham6.h"

python3 - "$ROOT/platform/common/plat_sdl.c" "$ROOT/Makefile" <<'PY'
from pathlib import Path
import sys,re
c=Path(sys.argv[1]); m=Path(sys.argv[2])
s=c.read_text(); mk=m.read_text(); mark='FRF_PICODRIVE_DIRECTCHIP_V0_1'
if mark not in s:
    needle='#include "plat_sdl.h"'
    if needle not in s: raise SystemExit('plat_sdl include anchor not found')
    s=s.replace(needle,needle+'\n#include "frf_pd_ham6.h" /* '+mark+' */',1)

    mm=re.search(r'void\s+plat_video_flip\s*\(\s*void\s*\)\s*\{',s)
    if not mm: raise SystemExit('plat_video_flip anchor not found')
    pos=mm.end()
    hook='''\n\t/* FRF_PICODRIVE_DIRECTCHIP_V0_1: g_screen_ptr is the completed\n\t * 16-bit frame at this boundary. HAM6 consumes it before SDL_Flip. */\n\tif (frf_pd_ham6_present_rgb565(g_screen_ptr, (unsigned)g_screen_width,\n\t      (unsigned)g_screen_height, (unsigned)g_screen_ppitch * 2U))\n\t\treturn;\n'''
    s=s[:pos]+hook+s[pos:]

    mm=re.search(r'void\s+plat_target_finish\s*\(\s*void\s*\)\s*\{',s)
    if not mm: raise SystemExit('plat_target_finish anchor not found')
    pos=mm.end(); s=s[:pos]+'\n\tfrf_pd_ham6_shutdown(); /* '+mark+' */'+s[pos:]
    c.write_text(s)
else: print('plat_sdl.c already patched')

obj='platform/common/frf_pd_ham6.o'
if obj not in mk:
    needle='OBJS += platform/common/plat_sdl.o platform/common/inputmap_kbd.o'
    if needle not in mk: raise SystemExit('generic plat_sdl Makefile anchor not found')
    mk=mk.replace(needle,needle+'\nOBJS += '+obj,1)
    m.write_text(mk)
else: print('Makefile already patched')
PY

echo "Patched standalone SDL PicoDrive tree: $ROOT"
echo "Use FRF_PD_VIDEO=ham6 on the Amiga to bypass SDL presentation."
