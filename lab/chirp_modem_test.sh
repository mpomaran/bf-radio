#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
impaired="$TEST_TMPDIR/impaired.pcm"
decoded="$TEST_TMPDIR/decoded.bin"

rlocation() {
  local path="$1"
  if [ -n "${RUNFILES_MANIFEST_FILE:-}" ]; then
    grep -m1 "^_main/$path " "$RUNFILES_MANIFEST_FILE" | cut -d' ' -f2-
  else
    printf '%s/%s/%s\n' "$TEST_SRCDIR" "$TEST_WORKSPACE" "$path"
  fi
}

resolve_tool() {
  local stem="$1"
  local tool
  tool="$(rlocation "lab/${stem}.exe")"
  if [ -z "$tool" ]; then
    tool="$(rlocation "lab/${stem}")"
  fi
  printf '%s\n' "$tool"
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"

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

printf 'chirp modem drift test payload' > "$payload"

encode_invocations=$((encode_invocations + 1))
"$modem" enc "$payload" "$encoded"
decode_invocations=$((decode_invocations + 1))
"$modem" dec "$encoded" "$decoded"
cmp -s "$payload" "$decoded"
print_timing_summary "clean_roundtrip"

# Whole-signal sample-rate mismatch. The decoder should infer the symbol span
# from sync and keep decoding with that drift estimate.
for scale in 95 96 97 98 99 101 102 103 104 105; do
  "$impair" "$encoded" "$impaired" --region all --scale-percent "$scale"
  decode_invocations=$((decode_invocations + 1))
  "$modem" dec "$impaired" "$decoded"
  cmp -s "$payload" "$decoded"
done
print_timing_summary "whole_signal_sample_rate_mismatch"

# Regional drift tests the neighboring-symbol tracker after sync has locked.
"$impair" "$encoded" "$impaired" --region middle --scale-percent 101 --region-percent 25
decode_invocations=$((decode_invocations + 1))
"$modem" dec "$impaired" "$decoded"
cmp -s "$payload" "$decoded"
print_timing_summary "regional_drift"

section_start_s=$test_start_s
print_timing_summary "total"
echo "Chirp modem roundtrip and drift tests OK"
