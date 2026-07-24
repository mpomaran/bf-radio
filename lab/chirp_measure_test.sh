#!/bin/bash
set -euo pipefail

rlocation() {
  local path="$1"
  if [ -n "${RUNFILES_MANIFEST_FILE:-}" ]; then
    grep -m1 "^_main/$path " "$RUNFILES_MANIFEST_FILE" | cut -d' ' -f2- || true
  else
    printf '%s/%s/%s\n' "$TEST_SRCDIR" "$TEST_WORKSPACE" "$path"
  fi
}

modem="$(rlocation lab/chirp_modem.exe)"
if [ -z "$modem" ]; then
  modem="$(rlocation lab/chirp_modem)"
fi

test_start_s=$(date +%s)
section_start_s=$test_start_s
encode_invocations=0
decode_invocations=0

print_timing_summary() {
  local label="$1"
  local now_s
  now_s=$(date +%s)
  echo "TIMING section=${label} wall_s=$((now_s - section_start_s)) encode_invocations=${encode_invocations} decode_invocations=${decode_invocations}"
  section_start_s=$now_s
}

out="$TEST_TMPDIR/measure_metric.csv"
err="$TEST_TMPDIR/measure_metric.err"
"$modem" measure-metric 500 >"$out" 2>"$err"
print_timing_summary "measure_metric_500"

grep -q '^profile,snr_db,trials,raw_ser,raw_ber,per$' "$out"
grep -q '^awgn-metric,12,500,' "$out"
grep -q '^radio-metric,12,500,' "$out"
grep -q '^awgn-metric,-6,500,' "$out"
grep -q '^radio-metric,-6,500,' "$out"
grep -q 'synthetic metric-channel model' "$err"

line_count="$(wc -l < "$out" | tr -d ' ')"
if [ "$line_count" -ne 15 ]; then
  echo "Unexpected measure row count: $line_count"
  cat "$out"
  exit 1
fi

alias_out="$TEST_TMPDIR/measure_alias.csv"
alias_err="$TEST_TMPDIR/measure_alias.err"
"$modem" measure 10 >"$alias_out" 2>"$alias_err"
print_timing_summary "measure_alias_10"
grep -q "legacy alias" "$alias_err"
grep -q '^profile,snr_db,trials,raw_ser,raw_ber,per$' "$alias_out"

pcm_out="$TEST_TMPDIR/measure_pcm.csv"
pcm_err="$TEST_TMPDIR/measure_pcm.err"
"$modem" measure-pcm 1 >"$pcm_out" 2>"$pcm_err"
print_timing_summary "measure_pcm_1"
if grep -q 'NOT SAME BITRATE' "$pcm_err"; then
  echo "Default measure-pcm unexpectedly printed NOT SAME BITRATE"
  cat "$pcm_err"
  exit 1
fi
grep -q '^profile,baud_profile,line_raw_bps,phy_version,protocol_version,snr_db,trials,' "$pcm_out"
grep -q 'same_bitrate_as_legacy,same_channel_as_legacy' "$pcm_out"
grep -q 'rx_raw_ser,rx_raw_ber,oracle_raw_ser,oracle_raw_ber' "$pcm_out"
grep -q 'timing_search_full_count,timing_search_local_count,timing_search_center_count,average_offsets_per_symbol' "$pcm_out"
grep -q '^awgn,current,250,1,1,24,1,' "$pcm_out"
grep -q '^radio,current,250,1,1,24,1,' "$pcm_out"

payload="$TEST_TMPDIR/default_payload.bin"
encoded="$TEST_TMPDIR/default_payload.pcm"
enc_err="$TEST_TMPDIR/default_enc.err"
printf 'default profile smoke payload' >"$payload"
encode_invocations=$((encode_invocations + 1))
"$modem" enc "$payload" "$encoded" 2>"$enc_err"
if grep -q 'NOT SAME BITRATE' "$enc_err"; then
  echo "Default enc unexpectedly printed NOT SAME BITRATE"
  cat "$enc_err"
  exit 1
fi

section_start_s=$test_start_s
print_timing_summary "total"
echo "Chirp measurement tests OK"
