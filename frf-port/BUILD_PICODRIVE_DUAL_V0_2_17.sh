#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [[ -z "$ROOT" || ! -f "$ROOT/Makefile.libretro" || ! -f "$ROOT/platform/libretro/libretro.c" ]]; then
  echo "Usage: $0 /path/to/notaz-picodrive" >&2
  exit 2
fi
ROOT="$(cd "$ROOT" && pwd)"

CC="${CC:-}"
if [[ -z "$CC" ]]; then
  for c in m68k-amigaos-gcc m68k-unknown-amigaos-gcc; do
    if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
  done
fi
[[ -n "$CC" ]] || { echo "ERROR: m68k AmigaOS GCC not found" >&2; exit 3; }

AR="${AR:-${CC%gcc}ar}"
command -v "$AR" >/dev/null 2>&1 || { echo "ERROR: archiver not found: $AR" >&2; exit 3; }

CPUFLAGS="${CPUFLAGS:--m68020}"
OPTFLAGS="${OPTFLAGS:--O3 -fomit-frame-pointer -fno-strict-aliasing -fwrapv -fsigned-char}"
OUT="${OUT:-$HERE/FRF_PicoDriveRGB_Amiga68k_V0_2_21_JOYFIX}"

echo "=== Future Retro Fusion PicoDriveRGB AmigaOS 68k v0.2.21 ==="
echo "tree: $ROOT"
echo "CC  : $CC"
echo "AR  : $AR"
echo "CPU : $CPUFLAGS"

echo "[0/4] Host/API compile preflight"
PREFLIGHT=(
  "$CPUFLAGS" $OPTFLAGS -fsyntax-only
  -I"$HERE/src"
  -I"$ROOT/platform/libretro/libretro-common/include"
  -I"$ROOT/platform/libretro/libretro-common/include/compat"
)
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_audio.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_input.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_timer.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_ham6.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_rtg.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_aga.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_keys.c"
"$CC" "${PREFLIGHT[@]}" "$HERE/src/frf_pd_coproc_client.c"
echo "  PASS: Amiga host modules parse before core rebuild"

echo "[1/4] Apply/upgrade Amiga static-core patch"
"$HERE/PATCH_PICODRIVE_AMIGA68K_MINI_ONLY.sh" "$ROOT"

echo "[2/4] Build static PicoDrive libretro core"
# Reuse existing pinned core objects; perform incremental rebuild
make -C "$ROOT" -f Makefile.libretro \
  platform=amiga68k \
  FRF_AMIGA_CC="$CC" FRF_AMIGA_AR="$AR" \
  FRF_AMIGA_CPUFLAGS="$CPUFLAGS" FRF_AMIGA_OPTFLAGS="$OPTFLAGS" \
  use_libchdr=0 use_sh2drc=0 use_cyclone=0 use_drz80=0 \
  PLATFORM_ZLIB=1 USE_LIBRETRO_VFS=0

CORE="$ROOT/picodrive_libretro_amiga68k.a"
[[ -f "$CORE" ]] || { echo "ERROR: expected static core missing: $CORE" >&2; exit 4; }

echo "[3/4] Link tiny native host + DirectChip presenter"
COMMON=(
  "$CPUFLAGS" $OPTFLAGS -ffunction-sections -fdata-sections
  -DFRF_PD_FORCE_HAM6=1
  -I"$HERE/src"
  -I"$ROOT/platform/libretro/libretro-common/include"
  -I"$ROOT/platform/libretro/libretro-common/include/compat"
)

# libretro.c always uses its portable case-insensitive substring helper for
# media detection. Unlike the PNG/Pico-overlay support, this one is real MD
# path functionality, so compile the upstream implementation into the host.
COMPAT_STRCASE="$ROOT/platform/libretro/libretro-common/compat/compat_strcasestr.c"
[[ -f "$COMPAT_STRCASE" ]] || { echo "ERROR: missing upstream compat helper: $COMPAT_STRCASE" >&2; exit 4; }

link_try() {
  local crt="$1"
  echo "  link CRT: ${crt:-default}"
  # shellcheck disable=SC2086
  "$CC" ${crt:+$crt} "${COMMON[@]}" \
    "$HERE/src/frf_pd_libretro_mini.c" \
    "$HERE/src/frf_pd_ham6.c" \
    "$HERE/src/frf_pd_rtg.c" \
    "$HERE/src/frf_pd_aga.c" \
    "$HERE/src/frf_pd_keys.c" \
    "$HERE/src/frf_pd_audio.c" \
    "$HERE/src/frf_pd_input.c" \
    "$HERE/src/frf_pd_timer.c" \
    "$HERE/src/frf_pd_coproc_client.c" \
    "$COMPAT_STRCASE" \
    "$CORE" -Wl,--gc-sections -lm -o "$OUT"
}

if ! link_try -mcrt=clib2; then
  echo "clib2 link failed; retrying compiler default CRT" >&2
  link_try ""
fi

"${CC%gcc}strip" "$OUT" 2>/dev/null || true

echo "[4/4] Result"
ls -lh "$OUT"
file "$OUT" 2>/dev/null || true

echo
echo "Copy to Amiga, then run:"
echo "  $(basename "$OUT") --frfcp=on --dbuf=on --renderer=accurate --audio-rate=44100 --frameskip=off Work:roms/game.md"
echo "A/B control: use --dbuf=off for the proven V0.2.15 CPU3 direct-front presenter."
echo "68K control: use --frfcp=off --dbuf=off."
echo "Future Retro Fusion v0.2.21: --dbuf=on enables optional HAM6 ScreenBuffer flipping; --dbuf=off remains the default. See docs/VIDEO_MODES.md for AGA / RTG."
