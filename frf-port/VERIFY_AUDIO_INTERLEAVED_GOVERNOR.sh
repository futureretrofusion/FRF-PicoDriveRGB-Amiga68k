#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
F="$HERE/src/frf_pd_libretro_mini.c"
fail(){ echo "FAIL: $*" >&2; exit 1; }
grep -q 'FRF_AUDIO_GUARD_CRITICAL_FRAMES 1536U' "$F" || fail critical
grep -q 'FRF_AUDIO_GUARD_LOW_FRAMES      4096U' "$F" || fail low
grep -q 'FRF_AUDIO_GUARD_HIGH_FRAMES     8192U' "$F" || fail high
grep -q 'g_audio_guard_critical_run<2U' "$F" || fail critical-bound
grep -q 'shed=(g_audio_guard_phase==0U)' "$F" || fail alternating-shed
grep -q 'g_audio_guard_unslept_this_frame' "$F" || fail per-frame-unsleep
grep -q 'INTERLEAVED critical=' "$F" || fail marker
if grep -q 'video work paused' "$F"; then fail old-burst-marker; fi
echo 'PASS: interleaved audio governor structure'
