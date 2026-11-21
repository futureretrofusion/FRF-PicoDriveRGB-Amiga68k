#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HAM="$HERE/src/frf_pd_ham6.c"
HDR="$HERE/src/frf_pd_ham6.h"
HOST="$HERE/src/frf_pd_libretro_mini.c"
fail(){ echo "FAIL: $*" >&2; exit 1; }

for f in "$HAM" "$HDR" "$HOST"; do [[ -f "$f" ]] || fail "missing $f"; done

grep -q 'frf_pd_ham6_set_double_buffer' "$HDR" || fail "dbuf API missing"
grep -q 'g_dbuf_enabled=0' "$HOST" || fail "dbuf must default OFF"
grep -q '"--dbuf"' "$HOST" || fail "--dbuf CLI missing"
grep -q 'AllocScreenBuffer(frf_screen,NULL,SB_SCREEN_BITMAP)' "$HAM" || fail "front ScreenBuffer wrapper missing"
grep -q 'AllocScreenBuffer(frf_screen,NULL,0)' "$HAM" || fail "back ScreenBuffer allocation missing"
grep -Fq 'frf_buffer_safe[i^1]=1;' "$HAM" || fail "SafeMessage N^1 ownership rule missing"
grep -Fq 'frf_buffer_displayed[i]=1;' "$HAM" || fail "DispMessage tracking missing"
grep -q 'ChangeScreenBuffer(frf_screen,frf_screen_buffers\[frf_back_index\])' "$HAM" || fail "page flip missing"
grep -q 'WaitBlit();' "$HAM" || fail "WaitBlit ownership barrier missing"
grep -q 'frf_dbuf_row_gen' "$HAM" || fail "row generation tracking missing"
grep -q 'frf_dbuf_applied_gen' "$HAM" || fail "per-buffer applied generations missing"
grep -q 'dbuf_note_cpu3_changes(arm_dirty,arm_h)' "$HAM" || fail "CPU3 dirty rows not accumulated"
grep -q 'path=%s' "$HAM" || fail "runtime path logging missing"
grep -q 'CPU3-DBUF' "$HAM" || fail "CPU3-DBUF marker missing"
grep -q 'dbuf_restore_screen_bitmap' "$HAM" || fail "safe shutdown ownership restore missing"
grep -q 'CPU3 equivalence PASS' "$HAM" || fail "CPU3 equivalence gate lost"
grep -q 'static void encode_pack_line' "$HAM" || fail "68K fallback lost"

echo "PASS: optional page-flip structure and ownership gates present"
echo "PASS: per-buffer row-generation correctness structure present"
echo "PASS: default remains --dbuf=off"
