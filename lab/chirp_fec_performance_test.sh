#!/bin/bash
set -euo pipefail

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

symbol_bytes=$((128 * 2))
preamble_bytes=$((48 * symbol_bytes))
sync_bytes=$((8 * symbol_bytes))
data_start=$((preamble_bytes + sync_bytes))
payload_sizes=(4 8 16 32 64 128 256 512 1024)
burst_sizes=(16 128)
data_burst_sizes=(16)

make_payload() {
  local size="$1"
  local file="$2"
  : > "$file"
  for ((i=0; i<size; ++i)); do
    printf '%b' "\\x$(printf '%02x' $((i % 256)))" >> "$file"
  done
}

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
  "$modem" dec "$2" "$3" >/dev/null 2>&1 && cmp -s "$1" "$3"
}

test_region() {
  local payload="$1"
  local encoded="$2"
  local decoded="$3"
  local label="$4"
  local offset="$5"
  local burst="$6"
  local test_file="$TEST_TMPDIR/${label}_${burst}.pcm"
  cp "$encoded" "$test_file"
  corrupt_burst "$test_file" "$offset" "$burst"
  roundtrip_ok "$payload" "$test_file" "$decoded"
}

echo "=== Chirp FEC performance matrix ==="
for payload_size in "${payload_sizes[@]}"; do
  payload="$TEST_TMPDIR/chirp_payload_${payload_size}.bin"
  encoded="$TEST_TMPDIR/chirp_encoded_${payload_size}.pcm"
  decoded="$TEST_TMPDIR/chirp_decoded_${payload_size}.bin"
  make_payload "$payload_size" "$payload"
  "$modem" enc "$payload" "$encoded" >/dev/null 2>&1
  encoded_size=$(wc -c < "$encoded")
  echo "PAYLOAD ${payload_size}B encoded=${encoded_size}B"
  roundtrip_ok "$payload" "$encoded" "$decoded" || { echo "FAIL clean roundtrip"; exit 1; }

  preamble_ok=0
  header_ok=0
  data_ok=0
  for burst in "${burst_sizes[@]}"; do
    if test_region "$payload" "$encoded" "$decoded" "preamble" 0 "$burst"; then preamble_ok="$burst"; fi
  done
  test_region "$payload" "$encoded" "$decoded" "sync" "$preamble_bytes" 4 || true
  test_region "$payload" "$encoded" "$decoded" "sync" "$preamble_bytes" 8 || true
  for burst in "${data_burst_sizes[@]}"; do
    if test_region "$payload" "$encoded" "$decoded" "header" "$data_start" "$burst"; then header_ok="$burst"; fi
  done
  mid_data_offset=$((data_start + (encoded_size - data_start) / 2))
  for burst in "${burst_sizes[@]}"; do
    if [ $((mid_data_offset + burst)) -ge "$encoded_size" ]; then break; fi
    if test_region "$payload" "$encoded" "$decoded" "data" "$mid_data_offset" "$burst"; then
      data_ok="$burst"
    fi
  done
  echo "  SUMMARY payload=${payload_size}B preamble_ok=${preamble_ok}B header_ok=${header_ok}B data_ok=${data_ok}B"
done

echo "Chirp FEC performance matrix completed"
