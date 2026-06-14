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
wav_to_pcm="$(find_runfile lab/wav_to_pcm)"
recording="$(rlocation testdata/received2.wav)"
expected="$(rlocation testdata/encoded_2.txt)"

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

pcm="$TEST_TMPDIR/received2.pcm"
decoded="$TEST_TMPDIR/decoded2.bin"
log="$TEST_TMPDIR/decode2.log"

"$wav_to_pcm" "$recording" "$pcm" 8000 >"$TEST_TMPDIR/convert2.log" 2>&1
print_timing_summary "wav_to_pcm"
decode_invocations=$((decode_invocations + 1))
timeout 420 "$chirp_modem" dec "$pcm" "$decoded" >"$log" 2>&1
print_timing_summary "decode"

if ! grep -q "\[progress\]" "$log"; then
  echo "Decoder did not emit progress output"
  cat "$log"
  exit 1
fi

cmp -s "$expected" "$decoded"
section_start_s=$test_start_s
print_timing_summary "total"
echo "Long recorded WAV decode/progress test OK"
