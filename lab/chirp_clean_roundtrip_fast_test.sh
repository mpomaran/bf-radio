#!/bin/bash
set -euo pipefail

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
decoded="$TEST_TMPDIR/decoded.bin"
decoded_full="$TEST_TMPDIR/decoded_full.bin"
decoded_center="$TEST_TMPDIR/decoded_center.bin"

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

printf 'fast clean chirp payload' > "$payload"
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
"$modem" dec "$encoded" "$decoded" >/dev/null 2>&1
cmp -s "$payload" "$decoded"
"$modem" --timing-search=full dec "$encoded" "$decoded_full" >/dev/null 2>&1
cmp -s "$payload" "$decoded_full"
"$modem" --timing-search=center dec "$encoded" "$decoded_center" >/dev/null 2>&1
cmp -s "$payload" "$decoded_center"
echo "Fast chirp clean roundtrip OK"
