#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HAM="$HERE/src/frf_pd_ham6.c"
INP="$HERE/src/frf_pd_input.c"
grep -Eq 'V0\.2\.16C: DBUF activation|V0\.2\.16A INPUT-ORDER FIX' "$HAM"
grep -q 'frf_dbuf_requested && !frf_dbuf_active && !frf_dbuf_attempted' "$HAM"
# Must not enable in init anymore.
python3 - "$HAM" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
a=s.index('int frf_pd_ham6_init_for_fps')
b=s.index('int frf_pd_ham6_present_rgb565',a)
if 'dbuf_try_enable();' in s[a:b]:
    raise SystemExit('FAIL: DBUF still activates before input init')
print('PASS: DBUF activation deferred past ham6 init')
PY
# Input source itself must retain classic fallback implementation.
grep -q 'gameport.device unit 1 fallback ACTIVE for P1' "$INP"
grep -q 'OpenDevice("gameport.device",1' "$INP"
echo 'PASS: V0.2.16C preserves deferred DBUF input-order repair structure'
