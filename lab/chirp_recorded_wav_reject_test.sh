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

pcm="$TEST_TMPDIR/recorded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
log="$TEST_TMPDIR/decode.log"

"$wav_to_pcm" "$recording" "$pcm" 8000 >"$TEST_TMPDIR/convert.log" 2>&1

set +e
timeout 20 "$chirp_modem" dec "$pcm" "$decoded" >"$log" 2>&1
rc=$?
set -e

if [ "$rc" -eq 124 ]; then
  echo "Decoder timed out on recorded WAV"
  cat "$log"
  exit 1
fi

if [ "$rc" -eq 0 ]; then
  echo "Recorded WAV unexpectedly decoded successfully"
  exit 1
fi

if ! grep -q "\[progress\]" "$log"; then
  echo "Decoder did not emit progress output"
  cat "$log"
  exit 1
fi

if ! grep -q "valid frame not found" "$log"; then
  echo "Decoder failed without the expected final diagnostic"
  cat "$log"
  exit 1
fi

echo "Recorded WAV rejection/progress test OK"
