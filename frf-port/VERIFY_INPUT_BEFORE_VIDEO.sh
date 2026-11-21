#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
MAIN="$ROOT/src/frf_pd_libretro_mini.c"
INPUT="$ROOT/src/frf_pd_input.c"
fail(){ echo "FAIL: $*" >&2; exit 1; }
grep -q 'INPUT OWNERSHIP REPAIR' "$MAIN" || fail 'repair marker missing'
python3 - "$MAIN" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
a=s.find('frf_pd_input_init()')
b=s.find('frf_pd_ham6_init_for_fps(g_core_fps)')
c=s.find('frf_pd_audio_init((unsigned)(av.timing.sample_rate+0.5))')
if min(a,b,c) < 0: raise SystemExit('FAIL: init calls missing')
if not (a < b < c): raise SystemExit(f'FAIL: wrong init order input={a} video={b} audio={c}')
print('PASS: input -> HAM6 video -> AHI startup order')
PY
grep -q 'OpenDevice("gameport.device",1' "$INPUT" || fail 'physical port 2 gameport missing'
grep -q 'GPD_READEVENT' "$INPUT" || fail 'gameport event reader missing'
grep -q 'GPCT_ABSJOYSTICK' "$INPUT" || fail 'absolute joystick claim missing'
echo 'PASS: V0.2.16B input-before-video repair structure'
