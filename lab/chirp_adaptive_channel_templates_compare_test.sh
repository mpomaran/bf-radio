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
  printf 'adaptive channel template comparison payload\n'
  printf 'deterministic multipath and filtered-channel regression data\n'
  printf 'seed=20260615\n'
} > "$payload"

"$modem" enc "$payload" "$original" >/dev/null 2>&1

ideal_failures=0
adaptive_failures=0
improved_or_equal=0

echo "case,ideal_ok,adaptive_ok"

run_case() {
  local name="$1"
  shift
  local impaired="$TEST_TMPDIR/${name}.pcm"
  local ideal_out="$TEST_TMPDIR/${name}_ideal.bin"
  local adaptive_out="$TEST_TMPDIR/${name}_adaptive.bin"
  local adaptive_err="$TEST_TMPDIR/${name}_adaptive.err"
  local ideal_ok=0
  local adaptive_ok=0

  "$impair" "$original" "$impaired" "$@" >/dev/null 2>&1

  if timeout 60 "$modem" --no-adaptive-channel-templates dec "$impaired" \
      "$ideal_out" >/dev/null 2>&1 && cmp -s "$payload" "$ideal_out"; then
    ideal_ok=1
  fi

  if timeout 60 "$modem" --adaptive-channel-templates --rx-diagnostics dec "$impaired" \
      "$adaptive_out" >/dev/null 2>"$adaptive_err" && \
      cmp -s "$payload" "$adaptive_out"; then
    adaptive_ok=1
  fi

  tr -d '\r' < "$adaptive_err" | grep -q '"adaptive_channel_templates_enabled":true'
  tr -d '\r' < "$adaptive_err" | grep -q '"channel_template_known_symbols":'
  tr -d '\r' < "$adaptive_err" | grep -q '"channel_template_energy":'
  tr -d '\r' < "$adaptive_err" | grep -q '"channel_template_fallback_used":'
  tr -d '\r' < "$adaptive_err" | grep -q '"ideal_vs_adaptive_sync_score":'

  echo "$name,$ideal_ok,$adaptive_ok"

  if [ "$ideal_ok" -eq 0 ]; then ideal_failures=$((ideal_failures + 1)); fi
  if [ "$adaptive_ok" -eq 0 ]; then adaptive_failures=$((adaptive_failures + 1)); fi
  if [ "$adaptive_ok" -ge "$ideal_ok" ]; then
    improved_or_equal=$((improved_or_equal + 1))
  fi
}

run_case echo_light \
  --leading-samples 1800 \
  --trailing-samples 1200 \
  --gain 0.82 \
  --time-scale 1.001 \
  --timing-wander-samples 0.08 \
  --fade-depth 0.04 \
  --echo1-delay 13 \
  --echo1-gain 0.08 \
  --echo2-delay 47 \
  --echo2-gain 0.02 \
  --noise-amplitude 45

run_case multipath_mild \
  --leading-samples 2200 \
  --trailing-samples 1600 \
  --gain 0.74 \
  --time-scale 1.002 \
  --timing-wander-samples 0.14 \
  --fade-depth 0.08 \
  --echo1-delay 21 \
  --echo1-gain 0.11 \
  --echo2-delay 63 \
  --echo2-gain 0.035 \
  --noise-amplitude 65

run_case radio_0p3pct \
  --leading-samples 2400 \
  --trailing-samples 1600 \
  --gain 0.76 \
  --time-scale 1.003 \
  --timing-wander-samples 0.15 \
  --fade-depth 0.10 \
  --echo1-delay 19 \
  --echo1-gain 0.09 \
  --echo2-delay 67 \
  --echo2-gain 0.025 \
  --noise-amplitude 75

if [ "$adaptive_failures" -gt "$ideal_failures" ]; then
  echo "Adaptive channel templates regressed: ideal_failures=$ideal_failures adaptive_failures=$adaptive_failures"
  exit 1
fi

if [ "$improved_or_equal" -lt 3 ]; then
  echo "Adaptive channel templates were worse in at least one deterministic case"
  exit 1
fi

echo "Adaptive channel template comparison OK"
