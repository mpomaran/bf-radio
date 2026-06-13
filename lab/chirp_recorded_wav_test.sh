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
recording="$(rlocation testdata/transmitted.wav)"
expected="$(rlocation testdata/transmitted.txt)"

pcm="$TEST_TMPDIR/recorded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
log="$TEST_TMPDIR/decode.log"

"$wav_to_pcm" "$recording" "$pcm" 8000 >"$TEST_TMPDIR/convert.log" 2>&1
timeout 20 "$chirp_modem" dec "$pcm" "$decoded" >"$log" 2>&1

if ! grep -q "\[progress\]" "$log"; then
  echo "Decoder did not emit progress output"
  cat "$log"
  exit 1
fi

cmp -s "$expected" "$decoded"
echo "Recorded WAV decode/progress test OK"
