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

find_runfile() {
  local path="$1"
  local candidate
  candidate="$(rlocation "$path.exe")"
  if [ -n "$candidate" ]; then
    printf '%s\n' "$candidate"
    return 0
  fi
  rlocation "$path"
}

json_value() {
  local file="$1"
  local field="$2"
  local value
  value="$(tr -d '\r\n' < "$file" |
    sed -n "s/.*\"${field}\":\\([^,}]*\\).*/\\1/p" |
    head -n 1 |
    tr -d '"')"
  if [ -z "$value" ]; then
    printf 'nan'
  else
    printf '%s' "$value"
  fi
}

now_ms() {
  local ns
  ns="$(date +%s%N 2>/dev/null || true)"
  if [[ "$ns" =~ ^[0-9]+$ ]]; then
    printf '%s' $((ns / 1000000))
  else
    printf '%s' "$(($(date +%s) * 1000))"
  fi
}

decode_mode() {
  local mode="$1"
  local profile="$2"
  local snr_db="$3"
  local impairment="$4"
  local pcm="$5"
  shift 5
  local flags=("$@")
  local out="$TEST_TMPDIR/${mode}_${impairment}.bin"
  local err="$TEST_TMPDIR/${mode}_${impairment}.err"
  local start_ms end_ms elapsed_ms
  local ok=0
  local fail=1

  start_ms="$(now_ms)"
  if timeout 60 "$modem" "${flags[@]}" --rx-diagnostics dec "$pcm" "$out" \
      >/dev/null 2>"$err" && cmp -s "$payload" "$out"; then
    ok=1
    fail=0
  fi
  end_ms="$(now_ms)"
  elapsed_ms=$((end_ms - start_ms))
  if [ "$elapsed_ms" -lt 0 ]; then elapsed_ms=0; fi

  local sync_score margin ldpc_iterations accepted_payload_ber
  sync_score="$(json_value "$err" "sync_score")"
  margin="$(json_value "$err" "margin_mean")"
  ldpc_iterations="$(json_value "$err" "ldpc_iterations_used")"
  accepted_payload_ber="nan"
  if [ "$ok" -eq 1 ]; then accepted_payload_ber="0"; fi

  printf '%s,%s,%s,%s,1,%s,%s,%s,nan,nan,%s,%s,%s,%s,%s\n' \
    "$mode" "$profile" "$snr_db" "$impairment" \
    "$ok" "$fail" "$fail" "$accepted_payload_ber" \
    "$sync_score" "$margin" "$ldpc_iterations" "$elapsed_ms"
}

run_all_modes() {
  local profile="$1"
  local snr_db="$2"
  local impairment="$3"
  local pcm="$4"

  decode_mode legacy "$profile" "$snr_db" "$impairment" "$pcm" --rx-profile=legacy
  decode_mode robust "$profile" "$snr_db" "$impairment" "$pcm" --rx-profile=robust
  decode_mode adaptive_llr "$profile" "$snr_db" "$impairment" "$pcm" \
    --rx-profile=legacy --adaptive-llr
  decode_mode adaptive_clock_tracking "$profile" "$snr_db" "$impairment" "$pcm" \
    --rx-profile=legacy --adaptive-clock-tracking
  decode_mode adaptive_channel_templates "$profile" "$snr_db" "$impairment" "$pcm" \
    --rx-profile=legacy --adaptive-channel-templates
  decode_mode weighted_correlation "$profile" "$snr_db" "$impairment" "$pcm" \
    --rx-profile=legacy --weighted-correlation
  decode_mode combined "$profile" "$snr_db" "$impairment" "$pcm" \
    --rx-profile=legacy --adaptive-llr --adaptive-clock-tracking \
    --adaptive-channel-templates --weighted-correlation
}

make_payload() {
  {
    printf 'robust receiver profile comparison payload\n'
    printf 'deterministic short packet for slow/manual CSV comparison\n'
    printf 'seed=20260616\n'
  } > "$payload"
}

make_radio_impairment() {
  local out="$1"
  local seed="$2"
  shift 2
  "$radio_impair" "$encoded" "$out" --seed "$seed" "$@" >/dev/null 2>&1
}

modem="$(find_runfile lab/chirp_modem)"
pcm_impair="$(find_runfile lab/pcm_impair)"
radio_impair="$(find_runfile lab/pcm_radio_channel_impair)"
audio_impair="$(find_runfile lab/pcm_audio_channel_impair)"

payload="$TEST_TMPDIR/payload.bin"
encoded="$TEST_TMPDIR/original.pcm"
make_payload
"$modem" enc "$payload" "$encoded" >/dev/null 2>&1

echo "mode,profile,snr_db,impairment,trials,packet_ok,packet_fail,per,rx_raw_ser,rx_raw_ber,accepted_payload_ber,mean_sync_score,mean_margin,mean_ldpc_iterations,mean_decode_ms"

clean_pcm="$TEST_TMPDIR/clean.pcm"
cp "$encoded" "$clean_pcm"
run_all_modes clean nan clean "$clean_pcm"

awgn_high="$TEST_TMPDIR/awgn_high.pcm"
make_radio_impairment "$awgn_high" 12001 \
  --leading-samples 0 --trailing-samples 0 --gain 1.0 \
  --time-scale 1.0 --timing-wander-samples 0 \
  --fade-depth 0 --echo1-gain 0 --echo2-gain 0 \
  --noise-amplitude 25 --impulse-amplitude 0
run_all_modes awgn 24 awgn_high_snr "$awgn_high"

awgn_near="$TEST_TMPDIR/awgn_near.pcm"
make_radio_impairment "$awgn_near" 12002 \
  --leading-samples 0 --trailing-samples 0 --gain 1.0 \
  --time-scale 1.0 --timing-wander-samples 0 \
  --fade-depth 0 --echo1-gain 0 --echo2-gain 0 \
  --noise-amplitude 180 --impulse-amplitude 0
run_all_modes awgn 12 awgn_near_threshold "$awgn_near"

radio24="$TEST_TMPDIR/radio24.pcm"
make_radio_impairment "$radio24" 13024 \
  --leading-samples 2400 --trailing-samples 1600 --gain 0.76 \
  --time-scale 1.003 --timing-wander-samples 0.15 \
  --fade-depth 0.10 --echo1-delay 19 --echo1-gain 0.09 \
  --echo2-delay 67 --echo2-gain 0.025 --noise-amplitude 75 \
  --impulse-interval 4096 --impulse-amplitude 650
run_all_modes radio 24 radio_24db "$radio24"

for noise in 120 180; do
  radio_weak="$TEST_TMPDIR/radio_weak_${noise}.pcm"
  make_radio_impairment "$radio_weak" $((14000 + noise)) \
    --leading-samples 2200 --trailing-samples 1200 --gain 0.74 \
    --time-scale 1.002 --timing-wander-samples 0.18 \
    --fade-depth 0.12 --echo1-delay 21 --echo1-gain 0.10 \
    --echo2-delay 63 --echo2-gain 0.035 --noise-amplitude "$noise" \
    --impulse-interval 4096 --impulse-amplitude 500
  run_all_modes radio "$noise" "radio_weak_noise_${noise}" "$radio_weak"
done

for ppm in -1000 -500 -200 -100 0 100 200 500 1000; do
  ppm_pcm="$TEST_TMPDIR/ppm_${ppm}.pcm"
  "$pcm_impair" "$encoded" "$ppm_pcm" --time-scale-ppm "$ppm" >/dev/null 2>&1
  run_all_modes clock nan "ppm_${ppm}" "$ppm_pcm"
done

echo_pcm="$TEST_TMPDIR/echo_multipath.pcm"
make_radio_impairment "$echo_pcm" 15001 \
  --leading-samples 1800 --trailing-samples 1200 --gain 0.82 \
  --time-scale 1.001 --timing-wander-samples 0.08 \
  --fade-depth 0.04 --echo1-delay 13 --echo1-gain 0.08 \
  --echo2-delay 47 --echo2-gain 0.02 --noise-amplitude 45 \
  --impulse-amplitude 0
run_all_modes radio nan echo_multipath "$echo_pcm"

audio_pcm="$TEST_TMPDIR/audio_channel.pcm"
"$audio_impair" "$encoded" "$audio_pcm" \
  --leading-samples 1800 --trailing-samples 1200 --gain 0.82 \
  --fade 0.08 --lowpass-alpha 0.72 --highpass-pole 0.996 \
  --echo-delay 17 --echo-gain 0.07 --noise-amplitude 45 --seed 16001 \
  >/dev/null 2>&1
run_all_modes audio nan audio_channel "$audio_pcm"

legacy_clean_err="$TEST_TMPDIR/legacy_clean.err"
legacy_clean_out="$TEST_TMPDIR/legacy_clean.bin"
"$modem" --rx-profile=legacy --rx-diagnostics dec "$clean_pcm" "$legacy_clean_out" \
  >/dev/null 2>"$legacy_clean_err"
grep -q '"rx_profile":"legacy"' "$legacy_clean_err"
grep -q '"same_bitrate_as_legacy":true' "$legacy_clean_err"
grep -q '"same_channel_as_legacy":true' "$legacy_clean_err"
cmp -s "$payload" "$legacy_clean_out"

robust_clean_err="$TEST_TMPDIR/robust_clean.err"
robust_clean_out="$TEST_TMPDIR/robust_clean.bin"
"$modem" --rx-profile=robust --rx-diagnostics dec "$clean_pcm" "$robust_clean_out" \
  >/dev/null 2>"$robust_clean_err"
grep -q '"rx_profile":"robust"' "$robust_clean_err"
grep -q '"adaptive_llr_enabled":true' "$robust_clean_err"
grep -q '"adaptive_clock_tracking_enabled":true' "$robust_clean_err"
grep -q '"adaptive_channel_templates_enabled":true' "$robust_clean_err"
grep -q '"weighted_correlation_enabled":true' "$robust_clean_err"
grep -q '"same_bitrate_as_legacy":true' "$robust_clean_err"
grep -q '"same_channel_as_legacy":true' "$robust_clean_err"
cmp -s "$payload" "$robust_clean_out"

echo "Robust receiver profile comparison OK" >&2
