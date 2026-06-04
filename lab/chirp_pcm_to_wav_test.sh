#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
wav_output="$TEST_TMPDIR/output.wav"

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

modem="$(resolve_tool chirp_modem)"
converter="$(resolve_tool pcm_to_wav)"

printf 'test' > "$payload"
"$modem" enc "$payload" "$encoded"
"$converter" "$encoded" "$wav_output"

[ -f "$wav_output" ]
[ "$(xxd -p -l 4 "$wav_output")" = "52494646" ]
[ "$(xxd -p -s 8 -l 4 "$wav_output")" = "57415645" ]
[ "$(xxd -p -s 12 -l 4 "$wav_output")" = "666d7420" ]
[ "$(xxd -p -s 36 -l 4 "$wav_output")" = "64617461" ]

echo "Chirp WAV validation passed"
