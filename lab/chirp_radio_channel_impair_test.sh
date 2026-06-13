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

find_runfile() {
  local path="$1"
  local candidate
  candidate="$(rlocation "$path.exe")"
  if [ -n "$candidate" ]; then
    printf '%s\n' "$candidate"
    return 0
  fi
  rlocation "$path"
}

chirp_modem="$(find_runfile lab/chirp_modem)"
impair="$(find_runfile lab/pcm_radio_channel_impair)"
original="$(rlocation testdata/original_encoded.pcm)"
expected="$(rlocation testdata/transmitted.txt)"

impaired="$TEST_TMPDIR/radio_channel.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
log="$TEST_TMPDIR/decode.log"

"$impair" "$original" "$impaired" \
  --leading-samples 2400 \
  --trailing-samples 1600 \
  --gain 0.76 \
  --time-scale 1.003 \
  --timing-wander-samples 0.18 \
  --fade-depth 0.10 \
  --echo1-delay 19 \
  --echo1-gain 0.09 \
  --echo2-delay 67 \
  --echo2-gain 0.025 \
  --noise-amplitude 75 \
  --impulse-interval 4096 \
  --impulse-amplitude 650 \
  >"$TEST_TMPDIR/impair.log" 2>&1

timeout 45 "$chirp_modem" dec "$impaired" "$decoded" >"$log" 2>&1
cmp -s "$expected" "$decoded"

echo "Synthetic radio-channel impairment test OK"
