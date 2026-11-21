#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
fail(){ echo "FAIL: $*" >&2; exit 1; }

MINI="$HERE/src/frf_pd_libretro_mini.c"
HAM="$HERE/src/frf_pd_ham6.c"
AUDIO="$HERE/src/frf_pd_audio.c"
TIMER="$HERE/src/frf_pd_timer.c"
BUILD="$HERE/BUILD_PICODRIVE_AMIGA68K_MINI.sh"

# Correctness-first defaults
grep -q 'g_renderer="accurate"' "$MINI" || fail "accurate renderer default missing"
grep -q 'g_frameskip="disabled"' "$MINI" || fail "frameskip-off default missing"
grep -q 'picodrive_renderer' "$MINI" || fail "renderer core option bridge missing"
grep -q 'g_audio_rate="22050"' "$MINI" || fail "22050 default missing"
grep -q -- '--audio-rate' "$MINI" || fail "audio-rate CLI missing"
grep -q 'if(data==NULL) { ++g_skipped; return; }' "$MINI" || fail "NULL frame still presented"
if grep -q 'signal(SIGINT' "$MINI"; then fail "async SIGINT handler still present"; fi

# Safe AHI worker
grep -q 'AHIST_S16S' "$AUDIO" || fail "AHI signed-16 output missing"
grep -q '#include <dos/dostags.h>' "$AUDIO" || fail "CreateNewProcTags header missing"
grep -q 'NP_Entry.*frf_audio_process_entry' "$AUDIO" || fail "dedicated AHI Process missing"
grep -q 'ahir_Link=NULL' "$AUDIO" || fail "AHI request is not explicitly unlinked"
if grep -qE 'SendIO\(|CheckIO\(|AbortIO\(' "$AUDIO"; then fail "unsafe async AHI request path remains"; fi
grep -q 'DoIO((struct IORequest \*)req)' "$AUDIO" || fail "single synchronous AHI request missing"

# V0.2.3 known-good HAM6 path restored
grep -q 'RGB565 -> fused HAM6 -> dirty UWORD CHIP runs -> physical RGB' "$HAM" || fail "known-good HAM6 marker missing"
grep -q 'copy_word_run' "$HAM" || fail "V0.2.3 dirty-run helper missing"
if grep -qE 'table-fast HAM6|changed plane-row bursts|frf_direct_error|frf_err_r\[16\]\[16\]' "$HAM"; then
  fail "experimental V0.2.4/V0.2.5 HAM6 path still present"
fi
if grep -qi 'saturat\|frf_sat_rgb4' "$HAM" "$HERE/src/frf_pd_ham6.h" "$MINI"; then fail "saturation filter reappeared"; fi

# No catch-up pacing
grep -q 'UNIT_MICROHZ' "$TIMER" || fail "microsecond timer device missing"
grep -q 'g_deadline=now;' "$TIMER" || fail "late-frame deadline rebase missing"
grep -q 'g_rem_acc=0;' "$TIMER" || fail "late-frame remainder reset missing"
if grep -q 'step\*4ULL' "$TIMER"; then fail "old catch-up clamp path remains"; fi
grep -q 'frf_pd_timer.c' "$BUILD" || fail "timer module not linked"

# Input/cleanup preserved
grep -q 'ReadJoyPort(0)' "$HERE/src/frf_pd_input.c" || fail "lowlevel port 0 missing"
grep -q 'OpenDevice("gameport.device",1' "$HERE/src/frf_pd_input.c" || fail "gameport fallback missing"
grep -q 'atexit(frf_cleanup)' "$MINI" || fail "idempotent atexit cleanup missing"
grep -q 'NTSC_MONITOR_ID' "$HAM" || fail "NTSC mode attempt missing"
grep -q 'PAL-FALLBACK' "$HAM" || fail "PAL fallback missing"

echo "PASS: V0.2.6 RECOVERY correctness/safety structure"

if [[ $# -ge 1 ]]; then
  ROOT="$1"
  [[ -f "$ROOT/platform/libretro/libretro.c" ]] || fail "not a PicoDrive tree: $ROOT"
  grep -q 'FRF_PICODRIVE_AMIGA68K_V0_1_2' "$ROOT/platform/libretro/libretro.c" || fail "Amiga static-core patch missing"
  echo "PASS: patched PicoDrive tree marker"
fi
