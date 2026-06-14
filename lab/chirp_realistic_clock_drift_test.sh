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

resolve_tool() {
  local stem="$1"
  local tool
  tool="$(rlocation "lab/${stem}.exe")"
  if [ -z "$tool" ]; then tool="$(rlocation "lab/${stem}")"; fi
  printf '%s\n' "$tool"
}

make_payload() {
  local size="$1"
  local seed="$2"
  local file="$3"
  local state="$seed"
  : > "$file"
  for ((i = 0; i < size; ++i)); do
    state=$(((1103515245 * state + 12345) & 0x7fffffff))
    printf '%b' "\\x$(printf '%02x' $(((state >> 8) & 0xff)))" >> "$file"
  done
}

append_silence() {
  local samples="$1"
  local file="$2"
  dd if=/dev/zero bs=$((samples * 2)) count=1 status=none >> "$file"
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"

ppms=(-1000 -500 -200 -100 -50 -20 0 20 50 100 200 500 1000)
payload_size=96

echo "ppm,time_scale,status,payload_size"
for ppm in "${ppms[@]}"; do
  payload="$TEST_TMPDIR/payload_${ppm}.bin"
  encoded="$TEST_TMPDIR/encoded_${ppm}.pcm"
  framed="$TEST_TMPDIR/framed_${ppm}.pcm"
  impaired="$TEST_TMPDIR/impaired_${ppm}.pcm"
  decoded="$TEST_TMPDIR/decoded_${ppm}.bin"
  make_payload "$payload_size" $((0xC001 + ppm + 2000)) "$payload"
  "$modem" enc "$payload" "$encoded" >/dev/null 2>&1
  : > "$framed"
  append_silence 4096 "$framed"
  cat "$encoded" >> "$framed"
  append_silence 4096 "$framed"
  "$impair" "$framed" "$impaired" --region all --time-scale-ppm "$ppm" >/dev/null 2>&1
  time_scale="$(awk -v ppm="$ppm" 'BEGIN { printf "%.9f", 1.0 + ppm / 1000000.0 }')"
  if "$modem" dec "$impaired" "$decoded" >/dev/null 2>&1 && cmp -s "$payload" "$decoded"; then
    echo "${ppm},${time_scale},OK,${payload_size}"
  else
    echo "${ppm},${time_scale},FAIL,${payload_size}"
    exit 1
  fi
done

echo "Realistic chirp audio clock drift ppm test OK"
