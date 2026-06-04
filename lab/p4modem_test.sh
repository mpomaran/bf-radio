#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"

printf 'test' > "$payload"
modem="$TEST_SRCDIR/$TEST_WORKSPACE/lab/p4modem"

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
