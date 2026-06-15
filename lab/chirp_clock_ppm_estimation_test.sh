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
  local file="$2"
  local state=0x5100
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

extract_json_number() {
  local key="$1"
  local file="$2"
  grep 'rx_diagnostics=' "$file" |
    sed -n "s/.*\"${key}\":\\([^,}]*\\).*/\\1/p" |
    head -1
}

modem="$(resolve_tool chirp_modem)"
impair="$(resolve_tool pcm_impair)"
tolerance_ppm=30
ppms=(-200 -100 -50 0 50 100 200)

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/encoded.pcm"
framed="$TEST_TMPDIR/framed.pcm"
make_payload 96 "$payload"
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1
: > "$framed"
append_silence 4096 "$framed"
cat "$encoded" >> "$framed"
append_silence 4096 "$framed"

echo "ppm,estimated_clock_ppm,error_ppm,clock_fit_error_rms_samples,clock_fit_points,status"
for ppm in "${ppms[@]}"; do
  impaired="$TEST_TMPDIR/impaired_${ppm}.pcm"
  decoded="$TEST_TMPDIR/decoded_${ppm}.bin"
  err="$TEST_TMPDIR/diag_${ppm}.err"
  "$impair" "$framed" "$impaired" --region all --time-scale-ppm "$ppm" >/dev/null 2>&1
  "$modem" --rx-diagnostics dec "$impaired" "$decoded" >/dev/null 2>"$err"
  cmp -s "$payload" "$decoded"

  estimated="$(extract_json_number estimated_clock_ppm "$err")"
  rms="$(extract_json_number clock_fit_error_rms_samples "$err")"
  points="$(extract_json_number clock_fit_points "$err")"
  if [ -z "$estimated" ] || [ -z "$rms" ] || [ -z "$points" ]; then
    echo "Missing clock-fit diagnostics for ppm=${ppm}"
    cat "$err"
    exit 1
  fi

  error_ppm="$(awk -v expected="$ppm" -v got="$estimated" \
    'BEGIN { err = got - expected; if (err < 0) err = -err; printf "%.6f", err }')"
  ok="$(awk -v err="$error_ppm" -v tol="$tolerance_ppm" \
    'BEGIN { print (err <= tol) ? "OK" : "FAIL" }')"
  echo "${ppm},${estimated},${error_ppm},${rms},${points},${ok}"
  if [ "$ok" != "OK" ]; then
    exit 1
  fi
  if [ "$points" -lt 4 ]; then
    echo "Too few clock-fit points for ppm=${ppm}: ${points}"
    exit 1
  fi
done

echo "Chirp clock ppm estimation test OK"
