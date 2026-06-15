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

modem="$(find_runfile lab/chirp_modem)"
impair="$(find_runfile lab/pcm_radio_channel_impair)"

payload="$TEST_TMPDIR/payload.bin"
original="$TEST_TMPDIR/original.pcm"

{
  printf 'weighted correlation deterministic comparison payload\n'
  printf 'colored noise, low pass, echo, mild timing wander\n'
  printf 'seed=20260615\n'
} > "$payload"

"$modem" enc "$payload" "$original" >/dev/null 2>&1

echo "case,baseline,adaptive_llr,adaptive_templates,weighted,combined"

decode_mode() {
  local mode_name="$1"
  local impaired="$2"
  local out="$TEST_TMPDIR/${mode_name}.bin"
  local err="$TEST_TMPDIR/${mode_name}.err"
  shift 2
  if timeout 60 "$modem" "$@" --rx-diagnostics dec "$impaired" "$out" \
      >/dev/null 2>"$err" && cmp -s "$payload" "$out"; then
    if [[ "$mode_name" == *weighted* || "$mode_name" == *combined* ]]; then
      tr -d '\r' < "$err" | grep -q '"weighted_correlation_enabled":true'
      tr -d '\r' < "$err" | grep -q '"weight_fallback_used":false'
    fi
    echo 1
  else
    echo 0
  fi
}

run_case() {
  local name="$1"
  shift
  local impaired="$TEST_TMPDIR/${name}.pcm"

  "$impair" "$original" "$impaired" "$@" >/dev/null 2>&1

  local baseline
  local adaptive_llr
  local adaptive_templates
  local weighted
  local combined
  baseline="$(decode_mode "${name}_baseline" "$impaired")"
  adaptive_llr="$(decode_mode "${name}_adaptive_llr" "$impaired" --adaptive-llr)"
  adaptive_templates="$(decode_mode "${name}_adaptive_templates" "$impaired" --adaptive-channel-templates)"
  weighted="$(decode_mode "${name}_weighted" "$impaired" --weighted-correlation)"
  combined="$(decode_mode "${name}_combined" "$impaired" --adaptive-llr --adaptive-channel-templates --weighted-correlation)"

  echo "$name,$baseline,$adaptive_llr,$adaptive_templates,$weighted,$combined"

  if [ "$weighted" -lt "$baseline" ]; then
    echo "Weighted correlation regressed baseline for $name"
    exit 1
  fi
  if [ "$combined" -lt "$baseline" ]; then
    echo "Combined mode regressed baseline for $name"
    exit 1
  fi
}

run_case colored_noise_lowpass \
  --leading-samples 1800 \
  --trailing-samples 1200 \
  --gain 0.78 \
  --time-scale 1.001 \
  --timing-wander-samples 0.08 \
  --fade-depth 0.05 \
  --echo1-delay 11 \
  --echo1-gain 0.05 \
  --echo2-delay 43 \
  --echo2-gain 0.015 \
  --noise-amplitude 50

run_case echo_multipath \
  --leading-samples 2200 \
  --trailing-samples 1600 \
  --gain 0.74 \
  --time-scale 1.002 \
  --timing-wander-samples 0.12 \
  --fade-depth 0.08 \
  --echo1-delay 21 \
  --echo1-gain 0.09 \
  --echo2-delay 67 \
  --echo2-gain 0.025 \
  --noise-amplitude 65

run_case cleanish_radio \
  --leading-samples 2000 \
  --trailing-samples 1400 \
  --gain 0.82 \
  --time-scale 1.0005 \
  --timing-wander-samples 0.05 \
  --fade-depth 0.03 \
  --echo1-delay 17 \
  --echo1-gain 0.04 \
  --echo2-delay 59 \
  --echo2-gain 0.01 \
  --noise-amplitude 35

echo "Weighted correlation comparison OK"
