#!/usr/bin/env bash
set -euo pipefail
# Convenience workflow. It intentionally does not assume a specific closed or
# third-party AmigaOS 68k libretro build recipe.
ROOT="${1:-$HOME/Downloads/picodrive}"
SELF="$(cd "$(dirname "$0")" && pwd)"
if [[ ! -d "$ROOT/.git" ]]; then
  echo "Cloning current upstream PicoDrive (with submodules)..."
  git clone --recursive https://github.com/notaz/picodrive.git "$ROOT"
fi
"$SELF/APPLY_LIBRETRO_DIRECTCHIP.sh" "$ROOT"
"$SELF/VERIFY_PATCH.sh" "$ROOT"
cat <<MSG

Source is patched.
For the first physical-hardware test, build the standalone smoke binary now:
  $SELF/BUILD_AMIGA_SMOKETEST.sh

For PicoDrive itself, invoke the AmigaOS 68k libretro build recipe/toolchain you
already use for that core. The patch is intentionally independent of the core's
68k build flags.
MSG
