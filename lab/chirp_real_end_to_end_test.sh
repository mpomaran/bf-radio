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

capture="$(rlocation testdata/chirp_real_end_to_end_8k.pcm)"
expected="$(rlocation testdata/chirp_real_end_to_end_expected.txt)"
decoded="$TEST_TMPDIR/decoded.txt"
stderr="$TEST_TMPDIR/decode.err"

"$modem" --rx-diagnostics dec "$capture" "$decoded" >/dev/null 2>"$stderr"
if [ "$(cat "$decoded")" != "$(cat "$expected")" ]; then
  echo "Decoded payload mismatch"
  echo "Expected: $(cat "$expected")"
  echo "Decoded:  $(cat "$decoded")"
  exit 1
fi
grep -q 'decoder: frame decoded from sliding window' "$stderr"
grep -q 'rx_diagnostics={' "$stderr"
echo "Real end-to-end chirp capture decoded OK"
