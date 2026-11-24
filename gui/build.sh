#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CC="${AMIGA_CC:-/opt/amiga/bin/m68k-amigaos-gcc}"
OUT="${1:-$ROOT/PicoDriveGUI}"
"$CC" -mcrt=clib2 -m68020 -O2 -Wall -Wextra "$ROOT/gui/PicoDriveGUI.c" -o "$OUT"
file "$OUT"
echo "Built: $OUT"
