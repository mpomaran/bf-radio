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

pcm_to_wav="$(find_runfile lab/pcm_to_wav)"
wav_to_pcm="$(find_runfile lab/wav_to_pcm)"

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

raw="$TEST_TMPDIR/input.pcm"
wav="$TEST_TMPDIR/input.wav"
roundtrip="$TEST_TMPDIR/roundtrip.pcm"
resampled="$TEST_TMPDIR/resampled.pcm"
wav8="$TEST_TMPDIR/pcm8.wav"
wav24="$TEST_TMPDIR/pcm24.wav"
wavf32="$TEST_TMPDIR/float32.wav"
converted="$TEST_TMPDIR/converted.pcm"
expected="$TEST_TMPDIR/expected.pcm"

: > "$raw"
for i in $(seq 0 255); do
  # Deterministic little-endian int16 ramp-ish waveform.
  v=$(( (i * 257) % 65536 ))
  lo=$(( v & 255 ))
  hi=$(( (v >> 8) & 255 ))
  printf '%b%b' "\\x$(printf '%02x' "$lo")" "\\x$(printf '%02x' "$hi")" >> "$raw"
done

"$pcm_to_wav" "$raw" "$wav" 8000 1 >/dev/null 2>&1
"$wav_to_pcm" "$wav" "$roundtrip" >/dev/null 2>&1
cmp -s "$raw" "$roundtrip"
print_timing_summary "pcm_wav_roundtrip"

"$pcm_to_wav" "$raw" "$wav" 16000 1 >/dev/null 2>&1
"$wav_to_pcm" "$wav" "$resampled" 8000 >/dev/null 2>&1
print_timing_summary "resample_16000_to_8000"
resampled_size=$(wc -c < "$resampled")
if [ "$resampled_size" -ne 256 ]; then
  echo "Expected 128 samples / 256 bytes after 16000->8000 conversion, got $resampled_size bytes"
  exit 1
fi

# 8-bit PCM: unsigned WAV samples 0, 128, 255 become int16 -32768, 0, 32512.
printf '\x52\x49\x46\x46\x27\x00\x00\x00\x57\x41\x56\x45' > "$wav8"
printf '\x66\x6d\x74\x20\x10\x00\x00\x00\x01\x00\x01\x00\x40\x1f\x00\x00' >> "$wav8"
printf '\x40\x1f\x00\x00\x01\x00\x08\x00\x64\x61\x74\x61\x03\x00\x00\x00' >> "$wav8"
printf '\x00\x80\xff' >> "$wav8"
printf '\x00\x80\x00\x00\x00\x7f' > "$expected"
"$wav_to_pcm" "$wav8" "$converted" >/dev/null 2>&1
cmp -s "$expected" "$converted"
print_timing_summary "pcm8"

# 24-bit PCM: signed samples -8388608, 0, 8388607 map to full-scale int16.
printf '\x52\x49\x46\x46\x2d\x00\x00\x00\x57\x41\x56\x45' > "$wav24"
printf '\x66\x6d\x74\x20\x10\x00\x00\x00\x01\x00\x01\x00\x40\x1f\x00\x00' >> "$wav24"
printf '\xc0\x5d\x00\x00\x03\x00\x18\x00\x64\x61\x74\x61\x09\x00\x00\x00' >> "$wav24"
printf '\x00\x00\x80\x00\x00\x00\xff\xff\x7f' >> "$wav24"
printf '\x00\x80\x00\x00\xff\x7f' > "$expected"
"$wav_to_pcm" "$wav24" "$converted" >/dev/null 2>&1
cmp -s "$expected" "$converted"
print_timing_summary "pcm24"

# 32-bit IEEE float: -1.0, 0.0, +1.0 map to clamped full-scale int16.
printf '\x52\x49\x46\x46\x30\x00\x00\x00\x57\x41\x56\x45' > "$wavf32"
printf '\x66\x6d\x74\x20\x10\x00\x00\x00\x03\x00\x01\x00\x40\x1f\x00\x00' >> "$wavf32"
printf '\x00\x7d\x00\x00\x04\x00\x20\x00\x64\x61\x74\x61\x0c\x00\x00\x00' >> "$wavf32"
printf '\x00\x00\x80\xbf\x00\x00\x00\x00\x00\x00\x80\x3f' >> "$wavf32"
printf '\x00\x80\x00\x00\xff\x7f' > "$expected"
"$wav_to_pcm" "$wavf32" "$converted" >/dev/null 2>&1
cmp -s "$expected" "$converted"
print_timing_summary "float32"

section_start_s=$test_start_s
print_timing_summary "total"
echo "WAV to PCM conversion tests OK"
