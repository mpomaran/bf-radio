#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"

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

if [ ! -x "$modem" ]; then
  chmod +x "$modem" || true
fi

"$modem" enc "$payload" "$encoded"
"$modem" dec "$encoded" "$decoded"

if ! cmp -s "$payload" "$decoded"; then
  echo "Decoded payload does not match original"
  echo "Original:"
  hexdump -C "$payload"
  echo "Decoded:"
  hexdump -C "$decoded"
  exit 1
fi

echo "Encode/decode roundtrip OK"
