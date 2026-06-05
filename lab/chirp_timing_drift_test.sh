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

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"
payload_sizes=(4 8 16 32 64 128 256 512)
regions=(all)
scales=(95 100 105)
bitflip_modes=(no_bitflips)

make_payload() {
  local size="$1"
  local file="$2"
  : > "$file"
  for ((i=0; i<size; ++i)); do
    printf '%b' "\\x$(printf '%02x' $((i % 256)))" >> "$file"
  done
}

decode_ok() {
  "$modem" dec "$2" "$3" >/dev/null 2>&1 && cmp -s "$1" "$3"
}

echo "=== Chirp timing drift matrix -5..+5% ==="
for payload_size in "${payload_sizes[@]}"; do
  payload="$TEST_TMPDIR/chirp_payload_${payload_size}.bin"
  encoded="$TEST_TMPDIR/chirp_encoded_${payload_size}.pcm"
  decoded="$TEST_TMPDIR/chirp_decoded_${payload_size}.bin"
  make_payload "$payload_size" "$payload"
  "$modem" enc "$payload" "$encoded" >/dev/null 2>&1
  decode_ok "$payload" "$encoded" "$decoded" || { echo "FAIL clean roundtrip payload=${payload_size}B"; exit 1; }
  echo "PAYLOAD ${payload_size}B"
  for region in "${regions[@]}"; do
    for bitflip in "${bitflip_modes[@]}"; do
      ok_scales=""
      fail_scales=""
      for scale in "${scales[@]}"; do
        impaired="$TEST_TMPDIR/chirp_timing_${payload_size}_${region}_${bitflip}_${scale}.pcm"
        if [ "$bitflip" = "with_bitflips" ]; then
          "$impair" "$encoded" "$impaired" --region "$region" --scale-percent "$scale" --region-percent 25 --bitflip-stride 4096
        else
          "$impair" "$encoded" "$impaired" --region "$region" --scale-percent "$scale" --region-percent 25
        fi
        if decode_ok "$payload" "$impaired" "$decoded"; then
          ok_scales="$ok_scales $((scale - 100))%"
        else
          fail_scales="$fail_scales $((scale - 100))%"
        fi
      done
      echo "  RESULT payload=${payload_size}B region=$region bitflips=$bitflip ok=[$ok_scales ] fail=[$fail_scales ]"
    done
  done
done

echo "Chirp timing drift matrix completed"
