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
  if [ -z "$tool" ]; then tool="$(rlocation "lab/${stem}")"; fi
  printf '%s\n' "$tool"
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"

printf 'fast drift chirp payload' > "$payload"
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
"$impair" "$encoded" "$impaired" --region all --scale-percent 101 >/dev/null 2>&1
"$modem" dec "$impaired" "$decoded" >/dev/null 2>&1
cmp -s "$payload" "$decoded"
echo "Fast chirp timing drift case OK"
