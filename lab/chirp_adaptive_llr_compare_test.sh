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

run_mode() {
  local mode="$1"
  local extra_flag="$2"
  local failures=0
  local trials=6
  for ((trial = 0; trial < trials; ++trial)); do
    local payload="$TEST_TMPDIR/payload_${mode}_${trial}.bin"
    local encoded="$TEST_TMPDIR/encoded_${mode}_${trial}.pcm"
    local impaired="$TEST_TMPDIR/impaired_${mode}_${trial}.pcm"
    local decoded="$TEST_TMPDIR/decoded_${mode}_${trial}.bin"
    make_payload 64 $((0xA500 + trial * 17)) "$payload"
    "$modem" enc "$payload" "$encoded" >/dev/null 2>&1
    "$radio_impair" "$encoded" "$impaired" \
      --time-scale 1.0005 \
      --timing-wander-samples 0.20 \
      --noise-amplitude 150 \
      --echo1-delay 9 \
      --echo1-gain 0.06 \
      --seed $((9000 + trial)) >/dev/null 2>&1
    if ! "$modem" $extra_flag dec "$impaired" "$decoded" >/dev/null 2>&1 ||
       ! cmp -s "$payload" "$decoded"; then
      failures=$((failures + 1))
    fi
  done
  echo "${mode},${trials},${failures}"
  return "$failures"
}

modem="$(resolve_tool chirp_modem)"
radio_impair="$(resolve_tool pcm_radio_channel_impair)"

echo "mode,trials,packet_errors"
set +e
fixed_line="$(run_mode fixed "")"
fixed_status=$?
adaptive_line="$(run_mode adaptive "--adaptive-llr")"
adaptive_status=$?
set -e
echo "$fixed_line"
echo "$adaptive_line"

if [ "$adaptive_status" -gt "$fixed_status" ]; then
  echo "Adaptive LLR regressed: fixed_errors=${fixed_status}, adaptive_errors=${adaptive_status}"
  exit 1
fi

echo "Adaptive LLR comparison OK"
