#!/bin/bash
set -euo pipefail

# Test: p4modem encode -> pcm_to_wav conversion
payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
wav_output="$TEST_TMPDIR/output.wav"

printf 'test' > "$payload"

rlocation() {
  local path="$1"
  if [ -n "${RUNFILES_MANIFEST_FILE:-}" ]; then
    grep -m1 "^_main/$path " "$RUNFILES_MANIFEST_FILE" | cut -d' ' -f2-
  else
    printf '%s/%s/%s\n' "$TEST_SRCDIR" "$TEST_WORKSPACE" "$path"
  fi
}

modem="$(rlocation lab/p4modem.exe)"
if [ -z "$modem" ]; then
  modem="$(rlocation lab/p4modem)"
fi
converter="$(rlocation lab/pcm_to_wav.exe)"
if [ -z "$converter" ]; then
  converter="$(rlocation lab/pcm_to_wav)"
fi

# Encode with p4modem
"$modem" enc "$payload" "$encoded"

# Convert PCM to WAV
"$converter" "$encoded" "$wav_output"

# Validate WAV file structure
if [ ! -f "$wav_output" ]; then
  echo "WAV file not created"
  exit 1
fi

wav_size=$(wc -c < "$wav_output")
if [ "$wav_size" -lt 44 ]; then
  echo "WAV file too small: $wav_size bytes (minimum 44 for header + data)"
  exit 1
fi

# Check for RIFF header signature
riff_sig=$(xxd -p -l 4 "$wav_output")
if [ "$riff_sig" != "52494646" ]; then  # "RIFF" in hex
  echo "Missing RIFF signature"
  exit 1
fi

# Check for WAVE header signature (should be at offset 8)
wave_sig=$(xxd -p -s 8 -l 4 "$wav_output")
if [ "$wave_sig" != "57415645" ]; then  # "WAVE" in hex
  echo "Missing WAVE signature"
  exit 1
fi

# Check for fmt  subchunk (should be at offset 12)
fmt_sig=$(xxd -p -s 12 -l 4 "$wav_output")
if [ "$fmt_sig" != "666d7420" ]; then  # "fmt " in hex
  echo "Missing fmt  subchunk"
  exit 1
fi

# Check for data subchunk
data_sig=$(xxd -p -s 36 -l 4 "$wav_output")
if [ "$data_sig" != "64617461" ]; then  # "data" in hex
  echo "Missing data subchunk at offset 36"
  exit 1
fi

echo "WAV validation passed"
echo "WAV file size: $wav_size bytes"
