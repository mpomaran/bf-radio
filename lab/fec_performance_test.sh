#!/bin/bash
# FEC performance matrix across payload sizes and channel burst lengths.

set -euo pipefail

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

symbol_bytes=$((80 * 2))
preamble_bytes=$((100 * symbol_bytes))
sync_bytes=$((16 * symbol_bytes))
data_start=$((preamble_bytes + sync_bytes))

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
  local original="$1"
  local pcm="$2"
  local decoded="$3"

  "$modem" dec "$pcm" "$decoded" >/dev/null 2>&1 && cmp -s "$original" "$decoded"
}

test_region() {
  local payload="$1"
  local encoded="$2"
  local decoded="$3"
  local label="$4"
  local offset="$5"
  local burst="$6"
  local test_file="$TEST_TMPDIR/${label//[^A-Za-z0-9]/_}_${burst}.pcm"

  cp "$encoded" "$test_file"
  corrupt_burst "$test_file" "$offset" "$burst"
  if roundtrip_ok "$payload" "$test_file" "$decoded"; then
    echo "  OK $label burst=${burst}B offset=$offset"
    return 0
  fi

  echo "  FAIL $label burst=${burst}B offset=$offset"
  return 1
}

echo "=== FEC performance matrix ==="
echo "Payload sizes: 4 8 16 32 64 128 256 512 1024 bytes"
echo "Burst scan: 1 2 4 8 16 32 64 128 256 512 1024 PCM bytes"
echo "Regions: preamble, sync, header-bearing early data, mid data"
echo ""

payload_sizes=(4 8 16 32 64 128 256 512 1024)
burst_sizes=(1 2 4 8 16 32 64 128 256 512 1024)

for payload_size in "${payload_sizes[@]}"; do
  payload="$TEST_TMPDIR/payload_${payload_size}.bin"
  encoded="$TEST_TMPDIR/encoded_${payload_size}.pcm"
  decoded="$TEST_TMPDIR/decoded_${payload_size}.bin"
  make_payload "$payload_size" "$payload"
  "$modem" enc "$payload" "$encoded" >/dev/null 2>&1
  encoded_size=$(wc -c < "$encoded")

  echo "PAYLOAD ${payload_size}B encoded=${encoded_size}B"

  if ! roundtrip_ok "$payload" "$encoded" "$decoded"; then
    echo "  FAIL clean roundtrip"
    exit 1
  fi
  echo "  OK clean roundtrip"

  preamble_limit=0
  header_limit=0
  data_limit=0
  first_data_failure="none"

  # Preamble corruption should be tolerated up to the point where sync search
  # can no longer align the packet. Scan until the first failure.
  for burst in "${burst_sizes[@]}"; do
    if test_region "$payload" "$encoded" "$decoded" "preamble" 0 "$burst"; then
      preamble_limit="$burst"
    else
      break
    fi
  done

  # Sync corruption is intentionally separate from preamble and FEC: it tests
  # whether the correlation receiver can still locate the packet.
  test_region "$payload" "$encoded" "$decoded" "sync" "$preamble_bytes" 4 || true
  test_region "$payload" "$encoded" "$decoded" "sync" "$preamble_bytes" 8 || true

  # Header bits are protected by LDPC and then interleaved across data symbols.
  # Corrupt the earliest data bytes, which carry header-related coded bits
  # before deinterleaving.
  for burst in "${burst_sizes[@]}"; do
    if test_region "$payload" "$encoded" "$decoded" "header" "$data_start" "$burst"; then
      header_limit="$burst"
    else
      break
    fi
  done

  mid_data_offset=$((data_start + (encoded_size - data_start) / 2))
  for burst in "${burst_sizes[@]}"; do
    if [ $((mid_data_offset + burst)) -ge "$encoded_size" ]; then
      break
    fi
    if test_region "$payload" "$encoded" "$decoded" "data" "$mid_data_offset" "$burst"; then
      data_limit="$burst"
    else
      first_data_failure="$burst"
      break
    fi
  done

  echo "  SUMMARY payload=${payload_size}B preamble_limit=${preamble_limit}B header_limit=${header_limit}B data_limit=${data_limit}B first_data_failure=${first_data_failure}"
  echo ""
done

echo "FEC performance matrix completed"
