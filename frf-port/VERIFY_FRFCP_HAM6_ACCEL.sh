#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
fail(){ echo "FAIL: $*" >&2; exit 1; }
ABI="$HERE/src/frf_coproc_abi.h"
CP="$HERE/src/frf_pd_coproc_client.c"
HAM="$HERE/src/frf_pd_ham6.c"
MINI="$HERE/src/frf_pd_libretro_mini.c"

grep -q 'FRFCP_FEAT_RGB565_TO_HAM6_PLANAR' "$ABI" || fail "HAM6 feature bit missing"
grep -q 'FRFCP_OP_RGB565_TO_HAM6_PLANAR.*6UL' "$ABI" || fail "HAM6 opcode 6 missing"
grep -q 'frfcp_ham6_prepare' "$CP" || fail "HAM6 prepare missing"
grep -q 'frfcp_ham6_encode_rgb565' "$CP" || fail "per-frame ARM job path missing"
grep -q 'FRFCP_OP_RGB565_TO_HAM6_PLANAR' "$CP" || fail "client does not submit HAM6 opcode"

grep -q 'frfcp_ham6_async_submit_rgb565' "$CP" || fail "async submit path missing"
grep -q 'frfcp_ham6_async_poll' "$CP" || fail "async poll path missing"
grep -Eq 'CPU3-PIPE|CPU3-DBUF' "$HAM" || fail "pipelined runtime marker missing"
if grep -n 'frfcp_ham6_encode_rgb565(pixels' "$HAM" >/dev/null; then fail "gameplay presenter still calls synchronous CPU3 encoder"; fi
grep -Eq 'HAM6 accelerator=CPU3|ARM CPU3 encode/pack ACTIVE' "$HAM" || fail "HAM presenter does not activate CPU3 path"
grep -q 'CPU3 equivalence PASS' "$HAM" || fail "real-hardware equivalence gate missing"
grep -q 'static void encode_pack_line' "$HAM" || fail "proven 68K fallback encoder missing"
grep -q 'copy_word_run' "$HAM" || fail "proven dirty-UWORD writer missing"
grep -q -- '--frfcp' "$MINI" || fail "A/B --frfcp switch missing"
if grep -qE 'table-fast HAM6|changed plane-row bursts|frf_direct_error|frf_err_r\[16\]\[16\]' "$HAM"; then
  fail "risky V0.2.4/V0.2.5 renderer code reintroduced"
fi
python3 "$HERE/VERIFY_HAM6_EQUIVALENCE.py"
if command -v cc >/dev/null 2>&1; then
  tmp="${TMPDIR:-/tmp}/frf_ham6_eq.$$"
  cc -O2 -std=c99 -Wall -Wextra -Werror "$HERE/VERIFY_HAM6_C_EQUIVALENCE.c" -o "$tmp"
  "$tmp"
  rm -f "$tmp"
fi
echo "PASS: CPU3 HAM6 pipeline structure + equivalence checks"
