#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-}"
if [[ -z "$CC" ]]; then
  for c in m68k-amigaos-gcc m68k-unknown-amigaos-gcc; do
    if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
  done
fi
[[ -n "$CC" ]] || { echo "ERROR: no m68k Amiga GCC found" >&2; exit 1; }
CPUFLAGS="${CPUFLAGS:--m68020}"
CFLAGS="${CFLAGS:--O3 -fomit-frame-pointer}"
OUT="$ROOT/FRF_PicoDriveRGB_HAM6_Smoke"

echo "Compiler: $CC"
echo "CPUFLAGS: $CPUFLAGS"
set +e
"$CC" $CPUFLAGS $CFLAGS -DFRF_PD_FORCE_HAM6=1 -mcrt=clib2 -I"$ROOT/src" \
  "$ROOT/src/frf_pd_ham6.c" "$ROOT/src/frf_pd_ham6_smoketest.c" \
  -o "$OUT"
rc=$?
set -e
if [[ $rc -ne 0 ]]; then
  echo "clib2 attempt failed; retrying with toolchain default CRT..."
  "$CC" $CPUFLAGS $CFLAGS -DFRF_PD_FORCE_HAM6=1 -I"$ROOT/src" \
    "$ROOT/src/frf_pd_ham6.c" "$ROOT/src/frf_pd_ham6_smoketest.c" \
    -o "$OUT"
fi
[[ -s "$OUT" ]] || { echo "ERROR: executable not produced" >&2; exit 1; }
sha256sum "$OUT" | tee "$ROOT/SMOKETEST_SHA256.txt"
echo
echo "Built: $OUT"
echo "Copy it to the Amiga and run:"
echo "  FRF_PicoDriveRGB_HAM6_Smoke"
