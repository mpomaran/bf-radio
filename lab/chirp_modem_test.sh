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

resolve_tool() {
  local stem="$1"
  local tool
  tool="$(rlocation "lab/${stem}.exe")"
  if [ -z "$tool" ]; then
    tool="$(rlocation "lab/${stem}")"
  fi
  printf '%s\n' "$tool"
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"

printf 'chirp modem drift test payload' > "$payload"

"$modem" enc "$payload" "$encoded"
"$modem" dec "$encoded" "$decoded"
cmp -s "$payload" "$decoded"

# Whole-signal sample-rate mismatch. The decoder should infer the symbol span
# from sync and keep decoding with that drift estimate.
for scale in 95 96 97 98 99 101 102 103 104 105; do
  "$impair" "$encoded" "$impaired" --region all --scale-percent "$scale"
  "$modem" dec "$impaired" "$decoded"
  cmp -s "$payload" "$decoded"
done

# Regional drift tests the neighboring-symbol tracker after sync has locked.
"$impair" "$encoded" "$impaired" --region middle --scale-percent 101 --region-percent 25
"$modem" dec "$impaired" "$decoded"
cmp -s "$payload" "$decoded"

echo "Chirp modem roundtrip and drift tests OK"
