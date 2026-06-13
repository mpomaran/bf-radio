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
expected="$(rlocation testdata/transmitted.txt)"

original="$TEST_TMPDIR/original_encoded.pcm"
impaired="$TEST_TMPDIR/audio_channel.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
log="$TEST_TMPDIR/decode.log"

"$chirp_modem" enc "$expected" "$original" >"$TEST_TMPDIR/encode.log" 2>&1
"$impair" "$original" "$impaired" >"$TEST_TMPDIR/impair.log" 2>&1
timeout 60 "$chirp_modem" dec "$impaired" "$decoded" >"$log" 2>&1
cmp -s "$expected" "$decoded"

echo "Synthetic audio-channel impairment test OK"
