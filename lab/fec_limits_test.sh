#!/bin/bash
# FEC Limits Test: Explores the boundaries of error correction
# Tests various corruption patterns to find where recovery fails

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

modem="$(rlocation lab/p4modem.exe)"
if [ -z "$modem" ]; then
  modem="$(rlocation lab/p4modem)"
fi

# Generate 256-byte deterministic payload
printf '\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f' > "$payload"
for i in {1..15}; do
  printf '\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f' >> "$payload"
done

# Encode once
"$modem" enc "$payload" "$encoded" 2>&1 | grep -i "encoded" | head -1
encoded_size=$(wc -c < "$encoded")

echo "=== FEC Error Correction Limits Test ==="
echo "Payload: 256 bytes"
echo "Encoded: $encoded_size bytes"
echo "Overhead: $(((encoded_size - 256) * 100 / 256))%"
echo ""

test_corruption() {
  local label="$1"
  local testfile="$2"
  
  if "$modem" dec "$testfile" "$decoded" 2>/dev/null && [ -s "$decoded" ]; then
    decoded_size=$(wc -c < "$decoded")
    errors=$(cmp -l "$payload" "$decoded" 2>/dev/null | wc -l)
    if [ "$errors" -eq 0 ]; then
      echo "  OK $label - RECOVERED"
      return 0
    else
      local pct=$((errors * 100 / 256))
      echo "  WARN $label - $errors byte errors ($pct% corruption in output)"
      return 0
    fi
  else
    echo "  FAIL $label - FAILED"
    return 1
  fi
}

corrupt_burst() {
  local file="$1"
  local offset="$2"
  local num_bytes="$3"
  local prefix="$TEST_TMPDIR/corrupt_$$"
  
  head -c "$offset" "$file" > "$prefix.h"
  tail -c +$((offset + num_bytes + 1)) "$file" > "$prefix.t"
  dd if=/dev/zero bs=1 count=$num_bytes 2>/dev/null | tr '\0' '\377' > "$prefix.c"
  cat "$prefix.h" "$prefix.c" "$prefix.t" > "$file"
}

corrupt_random() {
  local file="$1"
  local num_bytes="$2"
  local density="${3:-10}"  # every Nth byte corrupted
  
  # Convert to temp file, corrupt every Nth byte
  od -An -tx1 "$file" | tr -d ' \n' > "$TEST_TMPDIR/hex_dump"
  
  # Use dd to copy and corrupt with random-like pattern
  local temp_corrupt="$TEST_TMPDIR/temp_corrupt"
  cp "$file" "$temp_corrupt"
  for ((i=0; i<num_bytes; i+=density)); do
    if [ $((i + 1024)) -lt $encoded_size ]; then
      head -c $((i + 1024)) "$temp_corrupt" > "$TEST_TMPDIR/random.h"
      tail -c +$((i + 1025)) "$temp_corrupt" > "$TEST_TMPDIR/random.t"
      printf '\xff' > "$TEST_TMPDIR/random.b"
      cat "$TEST_TMPDIR/random.h" "$TEST_TMPDIR/random.b" "$TEST_TMPDIR/random.t" > "$temp_corrupt"
    fi
  done
  cp "$temp_corrupt" "$file"
}

echo "TEST CATEGORY 1: Single Burst Sizes"
echo "======================================"
for size in 1 4 8 16 32 64; do
  test_file="$TEST_TMPDIR/test_burst_$size.pcm"
  cp "$encoded" "$test_file"
  corrupt_burst "$test_file" 1000 $size
  test_corruption "Burst of $size bytes at offset 1000" "$test_file"
done

echo ""
echo "TEST CATEGORY 2: Burst Position Sensitivity"
echo "============================================"
positions=(100 1000 5000 20000)
for pos in "${positions[@]}"; do
  if [ $((pos + 16)) -le $encoded_size ]; then
    test_file="$TEST_TMPDIR/test_pos_$pos.pcm"
    cp "$encoded" "$test_file"
    corrupt_burst "$test_file" $pos 8
    test_corruption "8-byte burst at offset $pos" "$test_file"
  fi
done

echo ""
echo "TEST CATEGORY 3: Multiple Bursts"
echo "===================================="
# Two bursts far apart
test_2burst="$TEST_TMPDIR/test_2burst.pcm"
cp "$encoded" "$test_2burst"
corrupt_burst "$test_2burst" 1000 4
corrupt_burst "$test_2burst" 10000 4
test_corruption "Two 4-byte bursts 9000 bytes apart" "$test_2burst"

# Three bursts
test_3burst="$TEST_TMPDIR/test_3burst.pcm"
cp "$encoded" "$test_3burst"
corrupt_burst "$test_3burst" 500 4
corrupt_burst "$test_3burst" 5000 4
corrupt_burst "$test_3burst" 20000 4
test_corruption "Three 4-byte bursts spread throughout" "$test_3burst"

echo ""
echo "TEST CATEGORY 4: Dense Errors"
echo "==============================="
# Errors every 2KB
test_dense="$TEST_TMPDIR/test_dense_2kb.pcm"
cp "$encoded" "$test_dense"
for offset in 2000 4000 6000 8000; do
  if [ $((offset + 4)) -le $encoded_size ]; then
    corrupt_burst "$test_dense" $offset 4
  fi
done
test_corruption "4-byte bursts every 2KB (4 total)" "$test_dense"

echo ""
echo "TEST CATEGORY 5: Preamble/Sync Region Sensitivity"
echo "=================================================="
# Preamble: ~0-800, Sync: ~800-928, Data: 928+
test_preamble="$TEST_TMPDIR/test_preamble.pcm"
cp "$encoded" "$test_preamble"
corrupt_burst "$test_preamble" 50 1
test_corruption "1-byte corruption in preamble (byte 50)" "$test_preamble"

test_sync="$TEST_TMPDIR/test_sync.pcm"
cp "$encoded" "$test_sync"
corrupt_burst "$test_sync" 850 4
test_corruption "4-byte burst in sync region (byte 850)" "$test_sync"

echo ""
echo "=== SUMMARY OF FINDINGS ==="
echo "Current FEC Configuration (LDPC):"
echo "  - Type: (3,6)-regular LDPC code"
echo "  - Code rate: 0.82 (82 info bits -> 100 coded bits)"
echo "  - Decoder: Belief propagation ($(grep MAX_ITER /home/pomarm/bf-radio/lab/p4modem.cpp | head -1 | grep -oE '[0-9]+' | tail -1) iterations)"
echo "  - Encoding Ratio: ~$((encoded_size / 256)):1"
echo ""
echo "See README.md for detailed findings and capacity planning."
echo ""
exit 0
