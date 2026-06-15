#!/bin/bash
set -euo pipefail

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
  if [ -z "$tool" ]; then tool="$(rlocation "lab/${stem}")"; fi
  printf '%s\n' "$tool"
}

make_payload() {
  local size="$1"
  local seed="$2"
  local file="$3"
  local state="$seed"
  : > "$file"
  for ((i = 0; i < size; ++i)); do
    state=$(((1103515245 * state + 12345) & 0x7fffffff))
    printf '%b' "\\x$(printf '%02x' $(((state >> 8) & 0xff)))" >> "$file"
  done
}

append_silence() {
  local samples="$1"
  local file="$2"
  dd if=/dev/zero bs=$((samples * 2)) count=1 status=none >> "$file"
}

decode_matches() {
  local extra_flags="$1"
  local pcm="$2"
  local decoded="$3"
  local err="$4"
  # shellcheck disable=SC2086
  "$modem" $extra_flags dec "$pcm" "$decoded" >/dev/null 2>"$err" &&
    cmp -s "$payload" "$decoded"
}

check_adaptive_diagnostics() {
  local err="$1"
  tr -d '\r' < "$err" | grep -q '"adaptive_clock_tracking_enabled":true'
  tr -d '\r' < "$err" | grep -q '"clock_scale":'
  tr -d '\r' < "$err" | grep -q '"timing_error_before_rms":'
  tr -d '\r' < "$err" | grep -q '"timing_error_after_rms":'
}

run_case() {
  local name="$1"
  local impaired="$2"
  local decoded_base="$TEST_TMPDIR/decoded_base_${name}.bin"
  local decoded_adapt="$TEST_TMPDIR/decoded_adapt_${name}.bin"
  local err_base="$TEST_TMPDIR/base_${name}.err"
  local err_adapt="$TEST_TMPDIR/adapt_${name}.err"

  local base_ok=0
  local adapt_ok=0
  if decode_matches "" "$impaired" "$decoded_base" "$err_base"; then base_ok=1; fi
  if decode_matches "--adaptive-clock-tracking --rx-diagnostics" \
      "$impaired" "$decoded_adapt" "$err_adapt"; then
    adapt_ok=1
  fi
  if [ "$adapt_ok" -eq 1 ]; then
    check_adaptive_diagnostics "$err_adapt"
  fi
  echo "${name},${base_ok},${adapt_ok}"
  if [ "$adapt_ok" -lt "$base_ok" ]; then
    echo "Adaptive clock tracking regressed on ${name}"
    exit 1
  fi
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"
radio_impair="$(resolve_tool pcm_radio_channel_impair)"

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
framed="$TEST_TMPDIR/framed.pcm"
make_payload 96 0xC10C "$payload"
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
: > "$framed"
append_silence 4096 "$framed"
cat "$encoded" >> "$framed"
append_silence 4096 "$framed"

echo "case,baseline_ok,adaptive_ok"
for ppm in -1000 -500 -200 -100 100 200 500 1000; do
  impaired="$TEST_TMPDIR/ppm_${ppm}.pcm"
  "$impair" "$framed" "$impaired" --region all --time-scale-ppm "$ppm" >/dev/null 2>&1
  run_case "ppm_${ppm}" "$impaired"
done

radio_pcm="$TEST_TMPDIR/radio_003.pcm"
"$radio_impair" "$framed" "$radio_pcm" \
  --time-scale 1.003 \
  --timing-wander-samples 0.15 \
  --noise-amplitude 80 \
  --echo1-delay 9 \
  --echo1-gain 0.06 \
  --seed 4242 >/dev/null 2>&1
run_case "radio_0p3pct" "$radio_pcm"

echo "Adaptive clock tracking comparison OK"
