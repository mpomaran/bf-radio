#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
decoded_full="$TEST_TMPDIR/decoded_full.bin"
decoded_center="$TEST_TMPDIR/decoded_center.bin"
decoded_diag="$TEST_TMPDIR/decoded_diag.bin"
decoded_adaptive="$TEST_TMPDIR/decoded_adaptive.bin"
decoded_clock="$TEST_TMPDIR/decoded_clock.bin"
normal_err="$TEST_TMPDIR/normal_decode.err"
diag_err="$TEST_TMPDIR/diagnostic_decode.err"
adaptive_err="$TEST_TMPDIR/adaptive_decode.err"
clock_err="$TEST_TMPDIR/clock_decode.err"

rlocation() {
  local path="$1"
  if [ -n "${RUNFILES_MANIFEST_FILE:-}" ]; then
    grep -m1 "^_main/$path " "$RUNFILES_MANIFEST_FILE" | cut -d' ' -f2-
  else
    printf '%s/%s/%s\n' "$TEST_SRCDIR" "$TEST_WORKSPACE" "$path"
  fi
}

modem="$(rlocation lab/chirp_modem.exe)"
if [ -z "$modem" ]; then modem="$(rlocation lab/chirp_modem)"; fi

printf 'fast clean chirp payload' > "$payload"
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
"$modem" dec "$encoded" "$decoded" >/dev/null 2>"$normal_err"
cmp -s "$payload" "$decoded"
if grep -q 'rx_diagnostics=' "$normal_err"; then
  echo "Diagnostics unexpectedly printed without --rx-diagnostics"
  cat "$normal_err"
  exit 1
fi
"$modem" --timing-search=full dec "$encoded" "$decoded_full" >/dev/null 2>&1
cmp -s "$payload" "$decoded_full"
"$modem" --timing-search=center dec "$encoded" "$decoded_center" >/dev/null 2>&1
cmp -s "$payload" "$decoded_center"
"$modem" --rx-diagnostics dec "$encoded" "$decoded_diag" >/dev/null 2>"$diag_err"
cmp -s "$payload" "$decoded_diag"
grep -q 'rx_diagnostics={' "$diag_err"
grep -q '"sync_score":' "$diag_err"
grep -q '"estimated_clock_ppm":' "$diag_err"
grep -q '"clock_fit_error_rms_samples":' "$diag_err"
grep -q '"clock_fit_points":' "$diag_err"
grep -q '"adaptive_clock_tracking_enabled":false' "$diag_err"
grep -q '"clock_scale":' "$diag_err"
grep -q '"timing_error_before_rms":' "$diag_err"
grep -q '"timing_error_after_rms":' "$diag_err"
grep -q '"llr_saturation_rate":' "$diag_err"
grep -q '"adaptive_llr_enabled":false' "$diag_err"
grep -q '"adaptive_llr_scale":' "$diag_err"
grep -q '"known_symbol_margin_median":' "$diag_err"
grep -q '"known_symbol_count":' "$diag_err"
grep -q '"ldpc_decode_success":true' "$diag_err"
grep -q '"crc_ok":true' "$diag_err"
"$modem" --adaptive-llr --rx-diagnostics dec "$encoded" "$decoded_adaptive" >/dev/null 2>"$adaptive_err"
cmp -s "$payload" "$decoded_adaptive"
grep -q '"adaptive_llr_enabled":true' "$adaptive_err"
grep -q '"adaptive_llr_scale":' "$adaptive_err"
"$modem" --adaptive-clock-tracking --rx-diagnostics dec "$encoded" "$decoded_clock" >/dev/null 2>"$clock_err"
cmp -s "$payload" "$decoded_clock"
grep -q '"adaptive_clock_tracking_enabled":true' "$clock_err"
grep -q '"clock_scale":' "$clock_err"
echo "Fast chirp clean roundtrip OK"
