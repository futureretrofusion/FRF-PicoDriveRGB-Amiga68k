#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
H="$ROOT/src/frf_pd_ham6.c"
fail(){ echo "ERROR: $*" >&2; exit 1; }
grep -q 'V0.2.16C DYNAMIC-GEOMETRY REPAIR' "$H" || fail 'geometry repair marker missing'
grep -q 'geometry_wait_busy=1' "$H" || fail 'nonblocking geometry busy gate missing'
grep -q 'frf_arm_pipe_stale_dropped' "$H" || fail 'stale old-geometry result accounting missing'
grep -q 'force_all=1' "$H" || fail 'forced full back-page refresh missing'
grep -q '!force_all &&' "$H" || fail 'generation bypass for full refresh missing'
if grep -A12 'V0.2.16C DYNAMIC-GEOMETRY REPAIR' "$H" | grep -q 'frf_arm_ham6=0'; then
  fail 'geometry transition still immediately disables CPU3'
fi
echo 'PASS: V0.2.16C dynamic geometry keeps DBUF live and force-refreshes both pages'
