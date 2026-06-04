#!/bin/bash
# Timing drift test matrix for whole-signal and regional clock mismatch.

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
  if [ -z "$tool" ]; then
    tool="$(rlocation "lab/${stem}")"
  fi
  printf '%s\n' "$tool"
}

modem="$(resolve_tool p4modem)"
impair="$(resolve_tool pcm_impair)"

make_payload() {
  local size="$1"
  local file="$2"
  : > "$file"
  for ((i=0; i<size; ++i)); do
    printf '%b' "\\x$(printf '%02x' $((i % 256)))" >> "$file"
  done
}

decode_ok() {
  local original="$1"
  local pcm="$2"
  local decoded="$3"
  "$modem" dec "$pcm" "$decoded" >/dev/null 2>&1 && cmp -s "$original" "$decoded"
}

scan_until_failure() {
  local payload="$1"
  local encoded="$2"
  local decoded="$3"
  local payload_size="$4"
  local region="$5"
  local direction="$6"
  local bitflip="$7"
  local bitflip_stride="$8"

  local last_ok=0
  local first_fail="none"
  local sign=1
  if [ "$direction" = "shorten" ]; then
    sign=-1
  fi

  for pct in $(seq 1 20); do
    local scale=$((100 + sign * pct))
    local impaired="$TEST_TMPDIR/timing_${payload_size}_${region}_${direction}_${bitflip}_${pct}.pcm"
    if [ "$bitflip" = "with_bitflips" ]; then
      "$impair" "$encoded" "$impaired" --region "$region" --scale-percent "$scale" --region-percent 25 --bitflip-stride "$bitflip_stride"
    else
      "$impair" "$encoded" "$impaired" --region "$region" --scale-percent "$scale" --region-percent 25
    fi

    if decode_ok "$payload" "$impaired" "$decoded"; then
      last_ok="$pct"
    else
      first_fail="$pct"
      break
    fi
  done

  if [ "$first_fail" = "none" ]; then
    echo "  RESULT payload=${payload_size}B region=$region direction=$direction bitflips=$bitflip last_ok=${last_ok}% first_fail=none_through_20%"
  else
    echo "  RESULT payload=${payload_size}B region=$region direction=$direction bitflips=$bitflip last_ok=${last_ok}% first_fail=${first_fail}%"
  fi
}

payload_sizes=(4 8 16 32 64 128 256 512 1024)
regions=(all start middle end)
directions=(stretch shorten)
bitflip_modes=(no_bitflips with_bitflips)

echo "=== Timing drift matrix ==="
echo "Scale scan: 1% steps through 20%, stopping each case at first decode failure"
echo "Regions: all, start 25%, middle 25%, end 25%"
echo "Bit flips: off, or one deterministic byte bit flip every 4096 PCM bytes"
echo ""

for payload_size in "${payload_sizes[@]}"; do
  payload="$TEST_TMPDIR/payload_${payload_size}.bin"
  encoded="$TEST_TMPDIR/encoded_${payload_size}.pcm"
  decoded="$TEST_TMPDIR/decoded_${payload_size}.bin"
  make_payload "$payload_size" "$payload"
  "$modem" enc "$payload" "$encoded" >/dev/null 2>&1

  if ! decode_ok "$payload" "$encoded" "$decoded"; then
    echo "FAIL clean roundtrip payload=${payload_size}B"
    exit 1
  fi

  encoded_size=$(wc -c < "$encoded")
  echo "PAYLOAD ${payload_size}B encoded=${encoded_size}B"
  for region in "${regions[@]}"; do
    for direction in "${directions[@]}"; do
      for bitflip in "${bitflip_modes[@]}"; do
        scan_until_failure "$payload" "$encoded" "$decoded" "$payload_size" "$region" "$direction" "$bitflip" 4096
      done
    done
  done
  echo ""
done

echo "Timing drift matrix completed"
