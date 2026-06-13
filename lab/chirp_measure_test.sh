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

modem="$(rlocation lab/chirp_modem.exe)"
if [ -z "$modem" ]; then
  modem="$(rlocation lab/chirp_modem)"
fi

out="$TEST_TMPDIR/measure.csv"
"$modem" measure 500 >"$out"

grep -q '^profile,snr_db,trials,raw_ser,raw_ber,per$' "$out"
grep -q '^awgn-metric,12,500,' "$out"
grep -q '^radio-metric,12,500,' "$out"
grep -q '^awgn-metric,-6,500,' "$out"
grep -q '^radio-metric,-6,500,' "$out"

line_count="$(wc -l < "$out" | tr -d ' ')"
if [ "$line_count" -ne 15 ]; then
  echo "Unexpected measure row count: $line_count"
  cat "$out"
  exit 1
fi

echo "Chirp 500-packet measurement test OK"
