#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload_256.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"

rlocation() {
  local path="$1"
  if [ -n "${RUNFILES_MANIFEST_FILE:-}" ]; then
    grep -m1 "^_main/$path " "$RUNFILES_MANIFEST_FILE" | cut -d' ' -f2-
  else
    printf '%s/%s/%s\n' "$TEST_SRCDIR" "$TEST_WORKSPACE" "$path"
  fi
}

modem="$(rlocation lab/chirp_modem.exe)"
if [ -z "$modem" ]; then modem="$(rlocation lab/chirp_modem)"; fi

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

for ((i=0; i<256; ++i)); do
  printf '%b' "\\x$(printf '%02x' $i)" >> "$payload"
done

corrupt_burst() {
  local file="$1"
  local offset="$2"
  local num_bytes="$3"
  local prefix="$TEST_TMPDIR/corrupt_${offset}_${num_bytes}_$$"
  head -c "$offset" "$file" > "$prefix.h"
  tail -c +$((offset + num_bytes + 1)) "$file" > "$prefix.t"
  dd if=/dev/zero bs=1 count="$num_bytes" 2>/dev/null | tr '\0' '\377' > "$prefix.c"
  cat "$prefix.h" "$prefix.c" "$prefix.t" > "$file"
}

roundtrip_ok() {
  decode_invocations=$((decode_invocations + 1))
  "$modem" dec "$2" "$3" >/dev/null 2>&1 && cmp -s "$1" "$3"
}

encode_invocations=$((encode_invocations + 1))
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
roundtrip_ok "$payload" "$encoded" "$decoded" || { echo "FAIL clean roundtrip"; exit 1; }
print_timing_summary "setup_clean_roundtrip"
encoded_size=$(wc -c < "$encoded")
data_start=$(((48 + 8) * 128 * 2))
mid_data_offset=$((data_start + (encoded_size - data_start) / 2))

echo "=== Chirp FEC limits ==="
for burst in 1 4 8 16 32 64 128; do
  test_file="$TEST_TMPDIR/burst_${burst}.pcm"
  cp "$encoded" "$test_file"
  corrupt_burst "$test_file" "$mid_data_offset" "$burst"
  if roundtrip_ok "$payload" "$test_file" "$decoded"; then
    echo "OK mid-data burst=${burst}B"
  else
    echo "FAIL mid-data burst=${burst}B"
    exit 1
  fi
done

for offset in $((data_start)) $((data_start + 2048)) $mid_data_offset; do
  test_file="$TEST_TMPDIR/position_${offset}.pcm"
  cp "$encoded" "$test_file"
  corrupt_burst "$test_file" "$offset" 16
  if roundtrip_ok "$payload" "$test_file" "$decoded"; then
    echo "OK position offset=${offset}"
  else
    echo "FAIL position offset=${offset}"
    exit 1
  fi
done

section_start_s=$test_start_s
print_timing_summary "total"
echo "Chirp FEC limits test OK"
