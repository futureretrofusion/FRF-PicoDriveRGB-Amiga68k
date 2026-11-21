#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-$PWD/picodrive-frf-v0.2.0}"
PIN="26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee"

if [[ ! -d "$DEST/.git" ]]; then
  git clone --recursive https://github.com/notaz/picodrive.git "$DEST"
fi

git -C "$DEST" fetch --all --tags
git -C "$DEST" checkout "$PIN"
git -C "$DEST" submodule update --init --recursive

exec "$HERE/BUILD_PICODRIVE_AMIGA68K_MINI.sh" "$DEST"
