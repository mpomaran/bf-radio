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
decoded_channel_templates="$TEST_TMPDIR/decoded_channel_templates.bin"
decoded_ideal_templates="$TEST_TMPDIR/decoded_ideal_templates.bin"
decoded_weighted="$TEST_TMPDIR/decoded_weighted.bin"
normal_err="$TEST_TMPDIR/normal_decode.err"
diag_err="$TEST_TMPDIR/diagnostic_decode.err"
adaptive_err="$TEST_TMPDIR/adaptive_decode.err"
clock_err="$TEST_TMPDIR/clock_decode.err"
channel_templates_err="$TEST_TMPDIR/channel_templates_decode.err"
ideal_templates_err="$TEST_TMPDIR/ideal_templates_decode.err"
weighted_err="$TEST_TMPDIR/weighted_decode.err"

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
grep -q '"adaptive_channel_templates_enabled":false' "$diag_err"
grep -q '"channel_template_known_symbols":' "$diag_err"
grep -q '"channel_template_energy":' "$diag_err"
grep -q '"channel_template_fallback_used":true' "$diag_err"
grep -q '"ideal_vs_adaptive_sync_score":' "$diag_err"
grep -q '"weighted_correlation_enabled":false' "$diag_err"
grep -q '"weight_min":' "$diag_err"
grep -q '"weight_max":' "$diag_err"
grep -q '"weight_mean":' "$diag_err"
grep -q '"weight_fallback_used":false' "$diag_err"
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
"$modem" --adaptive-channel-templates --rx-diagnostics dec "$encoded" \
  "$decoded_channel_templates" >/dev/null 2>"$channel_templates_err"
cmp -s "$payload" "$decoded_channel_templates"
grep -q '"adaptive_channel_templates_enabled":true' "$channel_templates_err"
grep -q '"channel_template_fallback_used":false' "$channel_templates_err"
"$modem" --no-adaptive-channel-templates --rx-diagnostics dec "$encoded" \
  "$decoded_ideal_templates" >/dev/null 2>"$ideal_templates_err"
cmp -s "$payload" "$decoded_ideal_templates"
grep -q '"adaptive_channel_templates_enabled":false' "$ideal_templates_err"
grep -q '"channel_template_fallback_used":true' "$ideal_templates_err"
"$modem" --weighted-correlation --rx-diagnostics dec "$encoded" "$decoded_weighted" \
  >/dev/null 2>"$weighted_err"
cmp -s "$payload" "$decoded_weighted"
grep -q '"weighted_correlation_enabled":true' "$weighted_err"
grep -q '"weight_fallback_used":false' "$weighted_err"
echo "Fast chirp clean roundtrip OK"
