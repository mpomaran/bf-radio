#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
impaired="$TEST_TMPDIR/impaired.pcm"
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

for ((i=0; i<64; ++i)); do
  printf '%b' "\\x$(printf '%02x' $((i % 256)))" >> "$payload"
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

"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
cp "$encoded" "$impaired"
data_start=$(((48 + 8) * 128 * 2))
corrupt_burst "$impaired" "$data_start" 16
"$modem" dec "$impaired" "$decoded" >/dev/null 2>&1
cmp -s "$payload" "$decoded"
echo "Fast chirp FEC corruption case OK"
