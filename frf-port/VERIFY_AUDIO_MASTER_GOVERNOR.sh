#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="$HERE/src/frf_pd_libretro_mini.c"
AUDIO="$HERE/src/frf_pd_audio.c"
AH="$HERE/src/frf_pd_audio.h"
TIMER="$HERE/src/frf_pd_timer.c"
TH="$HERE/src/frf_pd_timer.h"
HAM="$HERE/src/frf_pd_ham6.c"
HH="$HERE/src/frf_pd_ham6.h"
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'FRF_AUDIO_GUARD_LOW_FRAMES   4096U' "$HOST" || fail "LOW watermark missing"
grep -q 'FRF_AUDIO_GUARD_HIGH_FRAMES 12288U' "$HOST" || fail "HIGH watermark missing"
grep -q 'frf_pd_audio_buffered_frames' "$HOST" || fail "buffered-frame telemetry not used"
grep -q 'frf_pd_timer_rebase_now' "$HOST" || fail "timer catch-up rebase missing"
grep -q 'frf_pd_ham6_mark_pipeline_stale' "$HOST" || fail "stale CPU3 suppression not used"
grep -q 'FRF_AHI_PREFILL_BLOCKS 4U' "$AUDIO" || fail "4096-frame AHI prefill missing"
grep -q 'unsigned frf_pd_audio_buffered_frames' "$AUDIO" || fail "audio buffered-frame API missing"
grep -q 'void frf_pd_timer_rebase_now' "$TIMER" || fail "timer rebase implementation missing"
grep -q 'void frf_pd_ham6_mark_pipeline_stale' "$HAM" || fail "HAM6 stale marker implementation missing"
grep -q 'frf_arm_pipe_stale_dropped' "$HAM" || fail "stale result counter missing"
# Guard must return before the expensive presenter call.
guard_line=$(grep -n 'if(g_audio_refill_active)' "$HOST" | head -1 | cut -d: -f1)
present_line=$(grep -n 'frf_pd_ham6_present_rgb565(data' "$HOST" | head -1 | cut -d: -f1)
[[ -n "$guard_line" && -n "$present_line" && "$guard_line" -lt "$present_line" ]] || fail "audio guard is not ahead of HAM6 presenter"
# Header contracts.
grep -q 'frf_pd_audio_buffered_frames' "$AH" || fail "audio header API missing"
grep -q 'frf_pd_timer_rebase_now' "$TH" || fail "timer header API missing"
grep -q 'frf_pd_ham6_mark_pipeline_stale' "$HH" || fail "HAM6 header API missing"

echo "PASS: V0.2.14 audio-master governor structure"
