#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[[ -n "$BIN" && -f "$BIN" ]] || { echo "Usage: $0 /path/to/FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX" >&2; exit 2; }
NAME="FRF_PicoDriveRGB_Amiga68k_V0_2_16C_DBUF_GEOMETRY_FIX"
OUT="$ROOT/release/FutureRetroFusion-PicoDriveRGB-v0.2.16c-AmigaOS68k.zip"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/FutureRetroFusion-PicoDriveRGB-v0.2.16c"
cp "$BIN" "$TMP/FutureRetroFusion-PicoDriveRGB-v0.2.16c/$NAME"
cp "$ROOT/README.md" "$ROOT/RELEASE_NOTES_v0.2.16C.md" "$ROOT/docs/LICENSING.md" "$TMP/FutureRetroFusion-PicoDriveRGB-v0.2.16c/"
sha256sum "$BIN" > "$TMP/FutureRetroFusion-PicoDriveRGB-v0.2.16c/SHA256SUMS.txt"
( cd "$TMP" && zip -qr "$OUT" "FutureRetroFusion-PicoDriveRGB-v0.2.16c" )
echo "$OUT"
sha256sum "$OUT"
