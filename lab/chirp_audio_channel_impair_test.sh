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
impair="$(find_runfile lab/pcm_audio_channel_impair)"
expected="$TEST_TMPDIR/expected.txt"
printf 'Synthetic audio channel regression payload.\n' >"$expected"

original="$TEST_TMPDIR/original_encoded.pcm"

"$chirp_modem" enc "$expected" "$original" >"$TEST_TMPDIR/encode.log" 2>&1

run_case() {
  local name="$1"
  shift
  local impaired="$TEST_TMPDIR/${name}.pcm"
  local decoded="$TEST_TMPDIR/${name}.bin"
  local log="$TEST_TMPDIR/${name}.decode.log"

  "$impair" "$original" "$impaired" "$@" >"$TEST_TMPDIR/${name}.impair.log" 2>&1
  timeout 60 "$chirp_modem" dec "$impaired" "$decoded" >"$log" 2>&1
  cmp -s "$expected" "$decoded"
  echo "audio_channel_case name=$name status=OK"
}

run_case measured_default
run_case quiet_awgn --audio-level quiet --snr-db 30 --noise-amplitude 90
run_case normal_voice_band --audio-level normal --bandpass 1 --bandpass-low-hz 300 --bandpass-high-hz 3000
run_case loud_clipped --audio-level loud --clip-percent 3 --snr-db 34 --noise-amplitude 70
run_case overdrive_clipped --audio-level overdrive --clip-percent 6 --noise-amplitude 50
run_case weak_clock_ppm --time-scale-ppm 1000
run_case squelch_drop --drop-count 1 --drop-min-ms 20 --drop-max-ms 20 --drop-depth 0.35 --noise-amplitude 100
run_case impulsive_noise --impulse-probability 0.0003 --impulse-amplitude 2200 --noise-amplitude 120

echo "Synthetic audio-channel impairment matrix OK"
