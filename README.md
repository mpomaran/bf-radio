# Audio Modem Experiments

This repository is an experimental audio modem playground for low-speed digital
communication over raw PCM/audio paths. It currently contains two waveform
experiments:

- `lab/chirp_modem.cpp`: the more advanced current prototype, using a
  chirp/CSS waveform, soft symbol metrics, Gray mapping, soft-value
  deinterleaving, local experimental FEC, timing-drift handling, and
  sliding-window acquisition tests.
- `lab/p4modem.cpp`: an older P4 polyphase modem experiment, useful as a
  comparison point and earlier waveform design.

The tests demonstrate repeatable packet recovery under specific synthetic PCM
impairments. They are not full RF-channel measurements and should not be read as
statistically complete BER/SER/PER-vs-SNR certification curves.

## Status

The code is a research prototype. It is intended for experiments with audio
modem framing, waveform acquisition, FEC layout, synthetic channel impairments,
and packet-level regression testing.

The current pass/fail tests are primarily CRC-gated packet recovery tests:

```text
payload -> encode -> PCM impairment -> decode -> protocol/CRC validation -> compare payload
```

Unless explicitly stated otherwise, measurements are at the application payload
boundary: a test passes only when the decoder returns a packet, protocol and CRC
checks pass, and the decoded payload exactly matches the transmitted payload.

## Implementations

### Chirp/CSS Modem: `lab/chirp_modem.cpp`

`chirp_modem.cpp` is a portable single-file C++17 implementation with no
external library dependencies.

Current implementation facts:

- Raw PCM input/output: 8 kHz, mono, signed 16-bit little-endian, no WAV header.
- Chirp/CSS waveform using LoRa-like cyclic shifts of an up-chirp.
- `ALPHABET = 16`, so one CSS symbol carries 4 raw bits before protocol/FEC
  overhead.
- `SYMBOL_SAMPLES = 128`, so one symbol is 128 samples = 16 ms = 256 PCM bytes.
- Nominal symbol rate: 62.5 symbols/s. Nominal raw uncoded bit rate:
  250 bit/s before FEC, pilots, preamble/sync, protected headers, CRC, and
  trailing silence.
- Preamble: 48 symbols of raw CSS symbol 0.
- Sync sequence: `15, 1, 14, 2, 13, 3, 12, 4`.
- PHY profile version: `1`. The measurement/accounting diagnostics do not by
  themselves change the over-the-air waveform, frame, FEC, or symbol layout.
- Pilot symbol: raw CSS symbol `10` inserted every 32 protected data symbols.
  Pilot overhead is part of the current legacy-compatible PHY profile.
- Protected frame header: magic `CHRP`, version `1`, payload length, flags.
- Maximum payload size: 4096 bytes.
- TX scrambles protected FEC bits with a deterministic PRBS before Gray/CSS
  mapping; RX descrambles by flipping LLR signs before FEC.
- TX maps 4-bit groups through Gray coding before CSS symbol selection.
- RX computes metrics for all 16 raw CSS symbols using a 128-point FFT circular
  correlation against the ideal or learned base chirp.
- With `--adaptive-channel-templates`, RX can learn a chirp template from known
  preamble/sync symbols, then use channel-shaped templates for the frame.
- With `--weighted-correlation`, RX can estimate conservative per-sample
  reliability weights from known-symbol residuals and use weighted time-domain
  correlation. The legacy receiver keeps the normal FFT correlation path; the
  robust receiver profile enables weighted correlation by default.
- RX derives bit LLRs using a max-log style best-0 vs best-1 comparison.
- Positive LLR means bit 0 is more likely; negative LLR means bit 1 is more
  likely.
- Soft values are deinterleaved before body FEC decoding.
- The header is encoded as its own FEC codeword before the interleaved body so
  streaming acquisition can estimate the required frame length.
- The FEC is a local experimental LDPC-style systematic sparse parity-check
  code: 64 information bits, 64 parity bits, 128-bit codeword, rate 1/2,
  full soft-decision sum-product belief-propagation decoder.
- The FEC decoder is now real BP, but the code is not standards-compatible
  WiFi, DVB-S2, or CCSDS LDPC.
- Final packet acceptance is gated by protocol magic/version, payload length,
  CRC16, and exact payload comparison in tests. FEC convergence is reported as
  diagnostics; a nonzero final syndrome is not silently hidden.
- Sync acquisition scans possible preamble starts, uses an energy-onset
  candidate, tries candidate symbol spans, and locally refines the best lock.
- Timing tracking uses a decision-directed loop with confidence gating and
  clamped symbol span updates.
- With `--adaptive-clock-tracking`, RX may use a receiver-estimated linear
  sample-clock model to choose data-symbol extraction positions. The default
  timing loop remains available and unchanged.
- A streaming scan API can classify rolling PCM windows as no frame, incomplete
  candidate, decoded frame, or rejected frame-like candidate.
- The default PHY reports `same_bitrate_as_legacy=true` and
  `same_channel_as_legacy=true`. The current receiver-only flags
  `--adaptive-llr`, `--adaptive-clock-tracking`,
  `--adaptive-channel-templates`, `--weighted-correlation`, and
  `--timing-search=...` do not change the transmitted waveform, channel
  occupancy, frame format, symbol count, pilot spacing, or FEC rate.
- A future non-legacy robustness profile that changes symbol length, alphabet
  size, sample rate, bitrate, latency, overhead, symbol count, pilot overhead,
  FEC rate, occupied channel, or frame duration must be clearly marked as
  `same_bitrate_as_legacy=false` and/or `same_channel_as_legacy=false` and must
  print:
  `NOT SAME BITRATE: this mode trades bitrate/latency/overhead for robustness.`

High-level transmit path:

```text
payload bytes
-> protected CHRP header + CRC16
-> header FEC and body FEC
-> body interleaver
-> PRBS bit scrambler
-> Gray-coded 4-bit groups
-> 16-shift chirp/CSS symbols
-> pilot insertion every 32 data symbols
-> preamble + sync + data/pilots + trailing silence
-> raw 8 kHz PCM16
```

High-level receive path:

```text
raw 8 kHz PCM16
-> streaming preamble/sync acquisition
-> optional preamble-adaptive channel template
-> FFT circular-correlation symbol metrics
-> optional weighted time-domain symbol metrics
-> optional adaptive clock sample-position prediction
-> pilot-aided timing/template updates
-> bit LLRs
-> PRBS LLR descrambler
-> body deinterleaver
-> FEC decoder
-> CHRP magic/version/length validation
-> CRC16 validation
-> payload bytes
```

### P4 Modem: `lab/p4modem.cpp`

`p4modem.cpp` is an older/alternative experiment based on cyclic shifts of a P4
polyphase sequence mixed onto a 1500 Hz audio carrier.

Current implementation facts:

- Raw PCM input/output: 8 kHz, mono, signed 16-bit little-endian.
- Symbol size: 80 samples = 10 ms = 160 PCM bytes.
- Alphabet size: 8 symbols, 3 raw bits per symbol before protocol/FEC overhead.
- Preamble: 100 symbols, roughly 1 second.
- Sync sequence: 16 known symbols.
- Frame body contains a 2-byte little-endian payload length, payload, and CRC16.
- FEC is a local systematic sparse parity-check code with 82 information bits,
  18 parity bits, 100-bit codeword, and hard-decision syndrome bit flipping.
- The P4 decoder uses hard symbol decisions and a fixed symbol timing model.
- P4 tests remain useful as regression tests for the earlier waveform, but the
  chirp/CSS modem has the more developed current acquisition and streaming
  behavior.

### Utilities

- `lab/pcm_impair.cpp`: applies deterministic synthetic PCM impairments:
  linear resampling/stretching over the whole stream or a selected region, plus
  optional byte-level bit flips.
- `lab/pcm_audio_channel_impair.cpp`: applies a deterministic
  speaker/recorder/microphone-style channel model with silence, gain envelope,
  simple filtering, echo, and noise.
- `lab/pcm_to_wav.cpp`: wraps raw PCM bytes in a WAV file for listening and
  debugging.
- `lab/wav_to_pcm.cpp`: converts RIFF/WAVE audio to raw modem PCM, including
  channel downmixing, bit-depth conversion, and sample-rate conversion.

## Quick Start

### Build With Bazel

From the repository root:

```bash
bazel build //lab:all
bazel test -c opt //lab:all
```

`bazel test -c opt //lab:all` is the normal fast development command. Heavy
matrix, long-recording, measurement, and torture tests are tagged `manual` so
they are not expanded by the `//lab:all` wildcard.

Equivalent explicit fast suite:

```bash
bazel test -c opt //lab:fast_tests
```

The fast suite keeps small correctness coverage for:

- clean chirp encode/decode roundtrip: `//lab:chirp_clean_roundtrip_fast_test`
- one realistic synthetic audio impairment: `//lab:chirp_audio_channel_impair_test`
- one FEC/corruption case: `//lab:chirp_fec_fast_test`
- one sample-rate/timing-drift case: `//lab:chirp_timing_drift_fast_test`
- module-level CRC, bit utility, interleaver, FEC, frame, FFT, waveform,
  modulator, and demod metric tests

Run the slower regression suite explicitly when you want broader coverage:

```bash
bazel test -c opt //lab:slow_tests --test_output=errors
```

Run the heaviest performance/torture tests explicitly:

```bash
bazel test -c opt //lab:performance_torture_tests --test_output=all
```

Individual slow tests can still be run directly, for example:

```bash
bazel test -c opt //lab:chirp_fec_performance_test --test_output=all
bazel test -c opt //lab:chirp_timing_drift_test --test_output=all
bazel test -c opt //lab:chirp_recorded_wav_long_test --test_output=all
```

Slow shell tests print `TIMING` summary lines with wall-clock seconds and the
number of shell-level encode/decode invocations per section. This is for test
cost accounting only and does not affect modem behavior.

Current validation note, 2026-06-16: `bazel test -c opt //lab:slow_tests
--test_output=errors` and `bazel test -c opt //lab:performance_torture_tests
--test_output=errors` pass in this workspace. `//lab:wav_to_pcm_test` is tagged
with a `moderate` timeout because it normally takes about 36-40 seconds on the
Windows/Bazel setup used for this run and can exceed Bazel's default small-test
timeout under parallel load.

`//lab:chirp_modem_test` is kept as an explicit manual diagnostic target for
the older broad 95..105% drift loop. It is tagged `known_failing` because that
case was already failing before this test-suite split. Use the focused fast
timing drift target for the default development regression.

Useful individual targets:

```bash
bazel build //lab:chirp_modem
bazel build //lab:p4modem
bazel build //lab:pcm_impair
bazel build //lab:pcm_audio_channel_impair
bazel build //lab:pcm_radio_channel_impair
bazel build //lab:pcm_to_wav
bazel build //lab:wav_to_pcm
```

### Build Chirp Modem Standalone With C++17

On Raspberry Pi OS or another system with `g++`:

```bash
cd lab
g++ -std=c++17 -O2 -Wall -Wextra -pedantic chirp_modem.cpp -o chirp_modem
./chirp_modem selftest
```

An optimized build may also be used:

```bash
g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -pedantic chirp_modem.cpp -o chirp_modem
```

### Encode and Decode Raw PCM

For the standalone chirp modem:

```bash
cd lab
./chirp_modem enc input.bin output.pcm
./chirp_modem dec output.pcm decoded.bin
./chirp_modem measure-pcm 1
cmp input.bin decoded.bin
```

With Bazel-built binaries from the repository root:

```bash
bazel-bin/lab/chirp_modem enc input.bin output.pcm
bazel-bin/lab/chirp_modem dec output.pcm decoded.bin
bazel-bin/lab/chirp_modem measure-pcm 1
cmp input.bin decoded.bin
```

### Chirp Measurement Modes

There are two measurement paths:

- `measure-metric [trials-per-snr]` is a fast synthetic metric-channel model.
  It does not run the PCM waveform receiver. The old `measure` command remains
  as a legacy alias and prints a warning.
- `measure-pcm [trials-per-snr]` runs generated PCM through the real waveform
  receiver path. Its CSV separates receiver-selected raw errors from oracle
  debug errors:

```text
rx_raw_ser,rx_raw_ber      receiver-selected hypothesis
oracle_raw_ser,oracle_raw_ber  best debug hypothesis using known TX bits
```

Use `rx_raw_*` and `PER` when judging modem quality. The `oracle_raw_*` columns
are only for diagnosing whether the receiver is losing performance in
acquisition/timing/template selection.

The PCM measurement also prints PHY/accounting columns such as PHY version,
protocol version, `same_bitrate_as_legacy`, `same_channel_as_legacy`, pilot
spacing, FEC rate, estimated occupied audio bandwidth, frame duration, net
payload bitrate, coded bitrate, overhead ratio, estimated Eb/N0, and coarse
failure counters for sync, header FEC, body FEC, CRC, false-lock, incomplete
frame, and converged-but-CRC-failed cases. The bandwidth number is an
intentionally simple estimator based on the configured chirp sweep from 700 Hz
to 2300 Hz plus guard margin; it is useful for accounting, not a regulatory
spectral mask measurement.

### Robust Receiver Defaults vs Legacy Baseline

The default chirp receiver profile is now `robust`. This is a receiver-only
default. It does not change the transmitted waveform, protected frame format,
FEC parameters, pilot spacing, symbol length, sample rate, alphabet size,
bitrate, occupied audio channel, PHY version, or protocol version.

The legacy baseline is available explicitly:

```bash
bazel-bin/lab/chirp_modem --rx-profile=legacy dec input.pcm output.bin
bazel-bin/lab/chirp_modem --legacy-receiver dec input.pcm output.bin
```

Legacy receiver definition:

```text
adaptive_llr=false
adaptive_clock_tracking=false
adaptive_channel_templates=false
weighted_correlation=false
timing_search=local unless overridden
```

Robust receiver definition:

```text
adaptive_llr=true
adaptive_clock_tracking=true
adaptive_channel_templates=true
weighted_correlation=true
timing_search=local unless overridden
```

Fine-grained flags override the selected profile, so for example
`--rx-profile=robust --no-weighted-correlation` uses the robust defaults except
for weighted correlation. Both profiles report `same_bitrate_as_legacy=true`
and `same_channel_as_legacy=true`. Robust receiver defaults must not print
`NOT SAME BITRATE`; that warning is reserved for future profiles that actually
change bitrate, latency, overhead, symbol count, pilot overhead, FEC rate,
frame duration, or occupied channel.

Deterministic measurements from this repository state:

| Command / scenario | Receiver | Trials | Packet OK | Packet fail | PER | Key notes |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `measure-pcm-debug --profile radio --snr 24 --trials 100` | legacy | 100 | 100 | 0 | 0.0000 | `no_failure_reproduced=1`, mean sync score 0.554280 |
| `measure-pcm-debug --profile radio --snr 24 --trials 100` | robust | 100 | 100 | 0 | 0.0000 | `no_failure_reproduced=1`, mean sync score 0.554280 |
| `compare-demod --profile radio --snr 9 --trials 100` | fixed max-log row | 100 | 100 | 0 | 0.0000 | `rx_raw_ser=0.941688`, `rx_raw_ber=0.337695`, `mean_peak_margin=0.640315` |
| `compare-demod --profile radio --snr 9 --trials 100` | calibrated max-log row | 100 | 100 | 0 | 0.0000 | same receiver-selected raw error rates in this deterministic run |

Exact commands used for this comparison:

```bash
bazel test -c opt //lab:slow_tests --test_output=all
bazel test -c opt //lab:performance_torture_tests --test_output=all
bazel-bin/lab/chirp_modem.exe --rx-profile=legacy measure-pcm-debug --profile radio --snr 24 --trials 100
bazel-bin/lab/chirp_modem.exe --rx-profile=robust measure-pcm-debug --profile radio --snr 24 --trials 100
bazel-bin/lab/chirp_modem.exe compare-demod --profile radio --snr 9 --trials 100
```

Validation status from the 2026-06-16 run:

- `//lab:slow_tests`: passed with `--test_output=errors`.
- `//lab:performance_torture_tests`: passed with `--test_output=errors`.
- `//lab:wav_to_pcm_test`: passed standalone in about 40 seconds and inside
  `//lab:slow_tests` in about 36 seconds after its Bazel timeout was changed to
  `moderate`. The timeout change is test accounting only; it does not change
  modem, WAV, PCM, or receiver behavior.

These measurements are regression evidence, not a production qualification.
They are not calibrated RF SNR measurements, not standards BER curves, and not
evidence of WiFi/DVB-S2/LoRa-grade link performance. The modem remains an
experimental audio/CSS prototype.

### Convert PCM to WAV

```bash
bazel-bin/lab/pcm_to_wav output.pcm output.wav 8000 1
```

The WAV converter defaults to 8000 Hz, mono, 16-bit PCM.

### Convert WAV to Modem PCM

```bash
bazel-bin/lab/wav_to_pcm input.wav output.pcm
bazel-bin/lab/chirp_modem dec output.pcm decoded.bin
```

The WAV input may have a different sample rate, channel count, or PCM/float
sample format. The converter writes raw 8000 Hz mono signed 16-bit little-endian
PCM by default. An explicit target sample rate may be supplied as the third
argument:

```bash
bazel-bin/lab/wav_to_pcm input.wav output.pcm 8000
```

## Signal and Protocol Format

### PCM Format

Both modem experiments use raw PCM rather than WAV on their encode/decode
interfaces:

```text
sample rate: 8000 Hz
channels:    1, mono
sample type: signed 16-bit little-endian
WAV header:  none
```

Conversions:

```text
1 second = 8,000 int16 samples = 16,000 PCM bytes
3 minutes = 180 seconds = 1,440,000 int16 samples = 2,880,000 PCM bytes
```

For the chirp/CSS modem:

```text
1 symbol = 128 samples = 16 ms = 256 PCM bytes
```

For the P4 modem:

```text
1 symbol = 80 samples = 10 ms = 160 PCM bytes
```

### Chirp/CSS Waveform

The chirp/CSS prototype creates an up-chirp over each 128-sample symbol and
represents raw CSS symbols as cyclic shifts. It uses 16 raw CSS symbols. The
receiver measures the cyclic shift of each symbol and uses the resulting metric
vector to compute bit LLRs.

Chirp spread spectrum is effective at weak SNR because each symbol spreads its
energy across a relatively long time-bandwidth product. After dechirping, a
correctly received chirp collapses into a narrow tone/bin while uncorrelated
noise remains spread across bins. That processing gain makes the decision depend
on energy accumulated over the whole symbol rather than on one instant in time.
The long chirp is also tolerant of narrowband interference and moderate timing
error because a local disturbance damages only part of the swept waveform.

LoRa implements this idea with complex dechirping followed by an FFT: multiply
the received chirp by a conjugate reference chirp, then choose the strongest FFT
bin. This prototype uses an equivalent scalar-friendly variant for the current
real-valued PCM waveform: it normalizes the received symbol, computes a 128-point
FFT circular correlation with the learned/ideal base chirp, and reads the 16 CSS
shift metrics from that correlation. The old per-symbol template correlator is
kept only as a fallback for invalid windows.

For clean synthetic PCM, the receiver can use the same ideal chirp templates as
the transmitter. For real audio paths, such as a laptop speaker recorded by a
phone and then replayed into a computer microphone, the waveform is no longer a
perfect copy of the generated chirp. With `--adaptive-channel-templates`, the
receiver averages known preamble symbols into a local channel-adapted chirp
template, refines it with the known sync symbols, and uses cyclic shifts of that
learned template for data demodulation.

This channel-template path is controlled by `--adaptive-channel-templates` and
`--no-adaptive-channel-templates`. It is enabled by the robust receiver profile
and disabled by the legacy receiver profile. It uses known preamble/sync/pilot
symbols to estimate channel-shaped templates for the current frame.

`--weighted-correlation` adds another receiver metric path. It estimates
per-sample reliability weights from residuals on known preamble/sync symbols,
clamps and normalizes those weights, then uses time-domain weighted correlation
for symbol decisions in that frame. This is intentionally conservative: legacy
mode or `--no-weighted-correlation` keeps the normal FFT correlation path, and
if too few known symbols are usable the weighted path falls back to unweighted
scoring.

When adaptive channel templates are enabled, full-frame demodulation also uses
conservative decision-directed template tracking. Only symbols with a high
best-vs-second-best correlation margin and a small timing offset are allowed to
update the learned base chirp. The selected raw CSS symbol is shifted back to
the base orientation, polarity is aligned against the current template, and the
base template is updated with a small exponential moving average. The
per-symbol template bank is rebuilt from that base template. This can help track
slow speaker/recorder/channel shape changes during long frames, but it is still
an experimental receiver-side aid, not a production channel estimator.

Candidate frames still must pass FEC and CRC validation; adaptive and
decision-directed templates only improve the symbol metrics.

The learned template has a known cyclic-shift orientation relative to the raw
CSS symbol numbering. The decoder normalizes that orientation before Gray
demapping, so the protocol bitstream remains the same as for ideal synthetic
templates.

Bit LLRs use a max-log best-zero versus best-one comparison over the 16 raw CSS
symbol metrics, with a conservative fixed scale and clipping before BP FEC. This
is not yet true calibrated noise-variance LLR estimation, but it gives the BP
decoder stronger soft information than raw correlation differences while staying
stable in timing-offset tests.

### Chirp/CSS Frame Structure

The chirp frame is:

```text
preamble: 48 symbols of raw CSS symbol 0
sync:     15, 1, 14, 2, 13, 3, 12, 4
data:     protected header FEC codeword + interleaved protected body
pilots:   raw CSS symbol 10 before data symbol 32, 64, 96, ...
tail:     0.25 seconds of silence
```

The protected header contains:

```text
magic:          "CHRP"
version:        1
payload length: uint16 little-endian
flags:          currently 0
```

The body contains:

```text
payload bytes
CRC16 over header + payload
```

The header is protected by FEC. It is not placed outside the encoded data. The
PRBS scrambler and pilot insertion are physical-layer transforms around the
protected bitstream; they do not change the CHRP payload format.

### FEC and Interleaving

The chirp/CSS modem uses a local experimental LDPC-style code:

```text
information bits: 64
parity bits:      64
codeword bits:    128
rate:             1/2
decoder:          full soft-decision sum-product belief propagation
```

This code is a compact experimental sparse parity-check code. It is not CCSDS,
DVB-S2, 802.11, or another standards-compatible LDPC code. The decoder exchanges
LLR-domain variable-to-check and check-to-variable messages on the Tanner graph,
uses the standard parity-check tanh/atanh sum-product update, clips messages for
numeric stability, and stops early when the hard-decision syndrome is clean.

The chirp body FEC bits are block-interleaved. On RX, symbol metrics are
converted to bit LLRs, body LLRs are deinterleaved, then FEC decoding is
attempted. Header FEC is intentionally separate so the streaming receiver can
learn the required frame size from a protected header before buffering the whole
body.

### Synchronization and Timing Tracking

The chirp receiver:

- scans for likely preamble/sync positions inside a PCM window,
- uses an energy-onset candidate for common leading-silence cases,
- tries a range of symbol spans for sample-rate offset,
- refines the selected lock,
- builds a preamble-adaptive channel template when a lock is strong enough,
- refines that template with the known sync word,
- tracks data timing with a decision-directed loop,
- updates timing only when the best-vs-second-best correlation margin is high
  enough,
- clamps the tracked symbol span to reduce runaway after bad decisions,
- updates the adaptive template during the frame only from high-confidence
  symbol decisions.

This is still a simple experimental timing loop, not a complete RF
synchronizer.

### Streaming Acquisition API

`chirp_modem.cpp` includes an internal scan API for continuous PCM listening:

```cpp
StreamScanResult scan_pcm_window_for_frame(const std::vector<int16_t>& pcm);
```

The result status can be:

- `NoFrameWindowConsumed`: no useful frame was found; the caller may erase
  `discard_prefix_samples`.
- `NeedMoreSamples`: a plausible candidate may be present, but the protected
  header or complete frame is not yet available; erase only
  `discard_prefix_samples`, keep the remaining tail, append more PCM, and scan
  again.
- `FrameDecoded`: a complete `CHRP` frame passed acquisition, FEC,
  magic/version/length validation, CRC, and payload extraction. The payload is
  returned.
- `InvalidFrameRejected`: a frame-like candidate was fully inspectable but was
  rejected, for example by wrong magic/version/header/CRC. The caller should
  erase `discard_prefix_samples` and continue scanning.

`discard_prefix_samples` is the number of samples from the front of the supplied
window that a sliding-window caller may safely remove. `frame_start_sample` and
`frame_end_sample` are offsets inside the supplied window; `frame_end_sample` is
exclusive.

## Error Tolerance and Test Methodology

### Measurement Boundary

Unless explicitly stated otherwise, current pass/fail tests are measured at the
application payload boundary. A pass means:

- the decoder returned a packet,
- protocol and CRC checks passed,
- decoded payload bytes exactly matched transmitted payload bytes.

This measurement boundary hides raw symbol and bit errors because failed
packets are rejected rather than counted as partial bit errors.

### Channel and SNR Terms

Use these terms consistently when interpreting modem results:

- **AWGN SNR** is additive white Gaussian noise power relative to signal power in
  a defined simulation boundary. In this repository it may refer either to a
  synthetic metric-domain model or to noise added to generated PCM samples. It
  is not automatically the same as RF receiver input SNR or calibrated Eb/N0.
- **Effective decision SNR** is the quality seen by the demodulator after sync,
  timing, filtering, correlation, LLR scaling, interleaving, and FEC. Multipath,
  clipping, wrong timing, bad LLR scaling, or false-lock ambiguity can reduce
  effective decision SNR even when sample-domain AWGN SNR looks high.
- **Timing drift** means the receiver's predicted symbol boundary slowly moves
  relative to the actual symbol boundary. In this modem it is handled by
  confidence-gated timing updates from data symbols and pilots, and optionally
  by `--adaptive-clock-tracking`.
- **Time-scale distortion** means the whole waveform, or a region of it, has
  been linearly stretched or compressed. This is how `pcm_impair` emulates
  sample-rate mismatch. Large scale factors are stress tests, not normal
  oscillator drift.
- **Multipath/echo** means delayed copies of the waveform are added to the
  direct path. Echo can create adjacent-symbol leakage, shifted correlation
  peaks, and burst-like symbol errors even without much random noise.
- **Clipping** limits sample amplitude and changes chirp shape. It can create
  harmonics and correlation side lobes; it is not equivalent to Gaussian noise.
- **AGC/fading** changes gain over time. Slow fading changes LLR scale and
  known-symbol margins; deep fades can create erasures or burst errors.

The deterministic `pcm_audio_channel_impair` and `pcm_radio_channel_impair`
tools combine some of these effects for repeatable regression testing. They are
not calibrated acoustic, CB, PMR, FM, or RF propagation models.

### SNR Strategy

Current SNR-related measurements are regression tools, not standards-grade link
budgets:

- Use `measure-metric` for fast algorithm comparisons in a synthetic
  metric-domain AWGN-like model. It is useful for checking FEC/LLR behavior and
  relative regressions, but it bypasses PCM sync/acquisition/timing.
- Use `measure-pcm --profile awgn` for end-to-end waveform tests with additive
  sample noise. This includes acquisition, timing, demodulation, FEC, and CRC,
  but still does not represent a calibrated RF receiver.
- Use `measure-pcm --profile radio` and the `pcm_*_channel_impair` tools for
  deterministic impairment regressions. Treat these as named stress profiles,
  not measured CB/PMR channel models.
- Use `rx_raw_ser`, `rx_raw_ber`, and `per` for receiver quality. The
  `oracle_raw_*` columns are debugging aids that answer whether a better
  candidate choice could have helped; they are not receiver performance.

Copy-pasteable examples:

```bash
bazel-bin/lab/chirp_modem measure-metric 100
bazel-bin/lab/chirp_modem measure-pcm 10
bazel-bin/lab/chirp_modem measure-pcm-debug --profile radio --snr 24 --trials 20
bazel-bin/lab/chirp_modem --adaptive-llr --weighted-correlation measure-pcm-debug --profile awgn --snr 18 --trials 20
```

### Current PCM SNR Sweep

Current deterministic end-to-end PCM measurements, 2026-06-16, robust receiver
defaults, 20 packets per SNR point:

```bash
bazel-bin/lab/chirp_modem.exe measure-pcm-sweep --profile awgn --trials 20
bazel-bin/lab/chirp_modem.exe measure-pcm-sweep --profile radio --trials 20
```

| Profile | SNR dB | PER | rx_raw_ser | rx_raw_ber | mean_peak_margin |
| --- | ---: | ---: | ---: | ---: | ---: |
| awgn | 3 | 0.00 | 0.119531 | 0.0426953 | 0.628307 |
| awgn | 0 | 0.00 | 0.170781 | 0.0598828 | 0.541372 |
| awgn | -3 | 0.00 | 0.244062 | 0.0847656 | 0.413307 |
| radio | 3 | 0.00 | 0.938594 | 0.353789 | 0.542896 |
| radio | 0 | 0.00 | 0.946875 | 0.371602 | 0.453099 |
| radio | -3 | 0.00 | 0.942969 | 0.436641 | 0.342762 |

Lower-SNR spot checks with `compare-demod` calibrated rows:

```bash
bazel-bin/lab/chirp_modem.exe compare-demod --profile awgn --snr -6 --trials 20
bazel-bin/lab/chirp_modem.exe compare-demod --profile radio --snr -6 --trials 20
bazel-bin/lab/chirp_modem.exe compare-demod --profile awgn --snr -9 --trials 20
bazel-bin/lab/chirp_modem.exe compare-demod --profile radio --snr -9 --trials 20
```

| Profile | SNR dB | PER | Dominant failure observation |
| --- | ---: | ---: | --- |
| awgn | -6 | 0.65 | body FEC failures dominate; sync/header usually still lock |
| awgn | -9 | 1.00 | mixed header/body/sync/false-lock failures |
| radio | -6 | 1.00 | false-lock/body-FEC failures; first debug failure was `false_lock` |
| radio | -9 | 1.00 | false-lock failures dominate |

Interpretation for this synthetic model: both AWGN and radio pass at `-3 dB` in
the 20-packet sweep, while the radio profile collapses by `-6 dB`. AWGN degrades
more gradually at `-6 dB` and fully collapses by `-9 dB` in the spot check. That
puts the current radio-model penalty at roughly 0-3 dB at packet-failure onset
and about 3 dB at full collapse, for this coarse grid only.

Debug excerpts at `-6 dB`:

```text
awgn:  trial=0 status=fail cause=body_fec sync_score=0.460641 header_raw_symbol_errors=7 header_raw_bit_errors=9 known_symbol_count=29 timing_error_rms=1.01322
radio: trial=0 status=fail cause=false_lock sync_score=0.272070 header_raw_symbol_errors=31 header_raw_bit_errors=54 known_symbol_count=0 timing_search_local_count=0
```

The radio-profile `rx_raw_ser`/`rx_raw_ber` columns are currently poor absolute
quality indicators under this impairment: they can look very high even when
packet PER is zero. Use packet PER, failure class, sync score, margin, and debug
diagnostics when judging the radio profile.

### Clock Drift Assumptions

For independent audio clocks, realistic sample-rate drift tests should usually
start in the tens to hundreds of ppm:

```bash
bazel test -c opt //lab:chirp_realistic_clock_drift_test --test_output=all
```

Recommended interpretation:

- Typical regression range: about `-200 ppm` to `+200 ppm`, with smaller points
  such as `-20`, `-50`, `+50`, and `+100 ppm` useful for sanity checks.
- Wider robustness range: about `-500 ppm` to `+1000 ppm`, useful for cheap
  sound devices, resampler mismatch, or pessimistic integration tests.
- `±5%` (`95%` to `105%` time scale) is not normal oscillator drift. It is a
  torture test for sample-rate mismatch, wrong conversion settings, or severe
  resampling distortion.

The ppm convention used by the impairment tool is:

```text
time_scale = 1.0 + ppm / 1e6
```

Copy-pasteable examples:

```bash
bazel-bin/lab/pcm_impair input.pcm output_200ppm.pcm --time-scale-ppm 200
bazel-bin/lab/pcm_impair input.pcm output_minus100ppm.pcm --time-scale-ppm -100
bazel-bin/lab/pcm_impair input.pcm output_torture_105.pcm --time-scale 1.05
```

### BER, SER, BLER, FER/PER

BER is bit errors divided by transmitted bits. It should only be used when a
test compares a known transmitted bitstream with a received bitstream at a
defined receiver boundary.

`./chirp_modem measure` reports raw coded-bit BER before FEC for synthetic AWGN
and radio-ish channels.

SER is symbol decision errors divided by transmitted symbols. It should only be
used when transmitted CSS/P4 symbol indices are compared against demodulated
symbol decisions.

`./chirp_modem measure` reports raw CSS SER for the same synthetic channels.

BLER is FEC codeword-level block error rate. FEC BLER is not currently measured
independently; the synthetic measurement reports packet error rate after full
frame decode and CRC validation.

Most current tests are CRC-gated packet recovery tests, closer to FER/PER
regression tests than BER tests.

False alarm rate or false acquisition rate would be false accepted frames per
unit time, stream duration, or scanned samples. The deterministic no-valid-frame
tests check that specific noise/fake-frame streams are rejected, but they do not
estimate statistical false alarm rate.

Missed detection rate is not currently measured statistically.

### Burst Overwrite Tests

The corruption tests use contiguous PCM burst overwrite, not BER injection.
The scripts overwrite selected byte ranges in the encoded PCM file with
`0xff` bytes.

Conversions for chirp/CSS:

```text
128 PCM bytes = 64 int16 samples = 8 ms = 0.5 chirp/CSS symbol
16 PCM bytes  = 8 int16 samples  = 1 ms = 0.0625 chirp/CSS symbol
```

Conversions for P4:

```text
160 PCM bytes = 80 int16 samples = 10 ms = 1 P4 symbol
```

Do not interpret a burst overwrite result as a BER number.

### Timing Drift / Sample-Rate Offset Tests

`pcm_impair` simulates sample-rate offset by linearly resampling either the
whole PCM stream or a selected region:

```bash
bazel-bin/lab/pcm_impair input.pcm output.pcm --region all --time-scale-ppm 200
bazel-bin/lab/pcm_impair input.pcm output.pcm --region all --time-scale 1.001
bazel-bin/lab/pcm_impair input.pcm output.pcm --region middle --scale-percent 101 --region-percent 25
```

`--time-scale-ppm 200` means `time_scale = 1.000200`. `--scale-percent 95`
creates an output stream with 95% of the original sample count for the selected
region. `--scale-percent 105` creates 105% of the original sample count. The
ppm mode is the appropriate one for ordinary clock-drift regression; the 95%
and 105% cases are sample-rate mismatch/torture tests, not realistic oscillator
drift, not Doppler-channel validation, and not BER tests.

`pcm_impair` can also apply deterministic byte-level bit flips with
`--bitflip-stride N --bitflip-mask M`.

### Speaker / Recorder / Microphone Channel Test

`pcm_audio_channel_impair` simulates the class of damage seen when a modem PCM
burst is played through speakers, recorded by a phone, and captured again
through a microphone:

```bash
pcm_audio_channel_impair input.pcm output.pcm
```

The model adds leading/trailing silence, gain envelope, DC blocking, simple
low-pass filtering, a short echo, and deterministic noise. It is a regression
tool for modem robustness, not a calibrated acoustic model.

With `--adaptive-channel-templates`, the chirp decoder learns a
channel-adapted chirp template from the repeated preamble symbols. It tries that
preamble-adapted template first, then falls back to the ideal synthetic template
when validation does not succeed.

Receiver diagnostics include `adaptive_channel_templates_enabled`,
`channel_template_known_symbols`, `channel_template_energy`,
`channel_template_fallback_used`, and `ideal_vs_adaptive_sync_score` so scripts
can compare ideal and channel-shaped templates without changing the transmitted
waveform.

When `--weighted-correlation` is enabled, diagnostics also include
`weighted_correlation_enabled`, `weight_min`, `weight_max`, `weight_mean`, and
`weight_fallback_used`. The comparison regression is:

```bash
bazel test -c opt //lab:chirp_weighted_correlation_compare_test --test_output=all
```

### Synthetic Radio Channel Test

`pcm_radio_channel_impair` is a deterministic raw PCM impairment tool for
radio-ish and difficult acoustic paths:

```bash
pcm_radio_channel_impair input.pcm output.pcm
```

The model applies leading/trailing idle, optional time-scale error, sinusoidal
timing wander, slow amplitude fading, DC blocking, one-pole low-pass bandwidth
limiting, two delayed echo/multipath taps, deterministic white noise,
deterministic impulsive noise, and clipping. It is meant for repeatable
regression tests. It is not an RF propagation model and its parameters are not
calibrated to a measured SNR, delay spread, fading distribution, or receiver
front end.

The default settings are moderate. More severe options are useful for
exploration, but a passing packet regression should always document the exact
parameters used.

### BER/SER/PER Measurement Mode

`chirp_modem` includes two quality measurement modes:

```bash
./chirp_modem measure-metric [trials-per-snr]  # fast statistical metric/LLR/FEC model
./chirp_modem measure [trials-per-snr]         # legacy alias with warning
./chirp_modem measure-pcm [trials-per-snr]     # real PCM waveform receiver path
./chirp_modem measure-pcm-debug --profile radio --snr 24 --trials 20
./chirp_modem measure-pcm-sweep --profile radio --trials 100
./chirp_modem compare-demod --profile radio --snr 9 --trials 100
```

All receiver commands also accept a global timing-search selector:

```bash
./chirp_modem --timing-search=local dec input.pcm output.bin   # default fast data path
./chirp_modem --timing-search=full measure-pcm 1               # old wide data search
./chirp_modem --timing-search=center measure-pcm-debug --profile awgn --snr 24 --trials 20
```

`full` checks the wide timing-offset set for every data symbol and is the
robust/torture-test mode. `local` keeps full search for acquisition, header
startup, pilots, and low-confidence recovery, then uses a small local data
symbol search while the timing loop is stable. `center` checks only the
predicted symbol center for data symbols and is mainly a clean-channel
benchmark mode. These options do not change the waveform, frame format, FEC, or
over-the-air compatibility.

The receiver also has a frame-local LLR calibration mode:

```bash
./chirp_modem --adaptive-llr dec input.pcm output.bin
./chirp_modem --adaptive-llr --llr-scale 0.5 --rx-diagnostics dec input.pcm output.bin
```

`--adaptive-llr` is enabled by the robust receiver profile and disabled by the
legacy profile. It estimates a robust confidence multiplier from known
preamble/sync/pilot symbols in the received frame. The configured `llr_scale`
remains the base multiplier, so `--llr-scale` can still be used as a manual
base scale. This changes only receiver-side soft metrics; it does not change
the waveform, bitrate, frame format, symbol alphabet, or FEC parameters.

Clock tracking has a separate adaptive mode:

```bash
./chirp_modem --adaptive-clock-tracking dec input.pcm output.bin
./chirp_modem --adaptive-clock-tracking --rx-diagnostics dec input.pcm output.bin
```

`--adaptive-clock-tracking` is enabled by the robust receiver profile and
disabled by the legacy profile. It uses the receiver-visible linear clock fit
from preamble/sync/pilot positions to predict data-symbol sample positions as
`offset + scale * nominal_position`. The existing pilot/timing loop remains
available; this option only changes receiver-side sample extraction positions
and does not change the over-the-air frame.

Channel-shaped templates and weighted correlation are also receiver-only
demodulator modes:

```bash
./chirp_modem --adaptive-channel-templates dec input.pcm output.bin
./chirp_modem --weighted-correlation dec input.pcm output.bin
./chirp_modem --adaptive-llr --adaptive-clock-tracking --adaptive-channel-templates --weighted-correlation dec input.pcm output.bin
```

`--adaptive-channel-templates` estimates a base chirp shape from known
preamble/sync symbols, generates shifted templates from that shape, and may
update the template from high-confidence pilots/data symbols. If the evidence
is weak or a candidate fails validation, the receiver can still fall back to
ideal templates.

`--weighted-correlation` estimates conservative per-sample reliability weights
from known-symbol residuals. It downweights sample positions whose residuals
look unreliable, clamps the weights, and falls back to ordinary unweighted
correlation if there are too few usable known symbols. This is not a full
noise-whitening equalizer; it is a simple receiver-side metric experiment.

All four adaptive flags above preserve the same transmitted waveform, bitrate,
symbol length, alphabet, pilot spacing, FEC, protected frame format, and
occupied audio channel. They should not print `NOT SAME BITRATE`.

Key `--rx-diagnostics` fields:

- `preamble_score`, `sync_score`, `detected_start_sample`,
  `estimated_symbol_span`: acquisition and coarse timing lock.
- `estimated_clock_ppm`, `clock_scale`, `clock_fit_error_rms_samples`,
  `clock_fit_points`: linear fit of observed known-symbol positions against
  nominal sample positions.
- `timing_error_before_rms`, `timing_error_after_rms`: pilot-observed timing
  error before/after the optional adaptive clock model, when enough comparison
  points exist.
- `timing_search_full_count`, `timing_search_local_count`,
  `timing_search_center_count`, `average_offsets_per_symbol`: receiver search
  cost and timing-search behavior.
- `winner_score_mean`, `runner_up_score_mean`, `margin_mean`, `margin_p05`,
  `margin_min`: symbol-decision separation. These are correlation metrics, not
  calibrated dB SNR.
- `llr_mean_abs`, `llr_max_abs`, `llr_saturation_count`,
  `llr_saturation_rate`, `llr_clip_value`, `llr_scale_used`: soft-bit metric
  scale and clipping behavior.
- `adaptive_llr_enabled`, `adaptive_llr_scale`,
  `known_symbol_margin_median`, `known_symbol_margin_p05`,
  `known_symbol_count`: frame-local LLR calibration state.
- `adaptive_channel_templates_enabled`, `channel_template_known_symbols`,
  `channel_template_energy`, `channel_template_fallback_used`,
  `ideal_vs_adaptive_sync_score`: channel-template availability and rough
  known-symbol score delta versus ideal templates.
- `weighted_correlation_enabled`, `weight_min`, `weight_max`, `weight_mean`,
  `weight_fallback_used`: weighted-correlation state.
- `ldpc_iterations_used`, `ldpc_decode_success`, `fec_failed_blocks`,
  `fec_syndrome_weight`, `crc_ok`, `decode_success`, `failure_cause`: FEC and
  final packet validation.

`measure-metric` generates deterministic 64-byte payloads, encodes real
protected modem frames, simulates a CSS metric vector for each transmitted
symbol, feeds those soft metrics through the real interleaver and BP FEC
decoder, and prints CSV:

```text
profile,snr_db,trials,raw_ser,raw_ber,per
```

Profiles:

- `awgn-metric`: idealized 16-way CSS correlator metrics plus Gaussian metric
  noise.
- `radio-metric`: the same metric model with 4 dB effective implementation
  loss, adjacent-symbol leakage, and deterministic short burst erasures.

`measure-pcm` is the slower end-to-end PCM path. It generates real audio PCM,
applies AWGN or a radio-ish PCM channel, runs sync/acquisition/demod/FEC/CRC,
and reports CSV with PHY accounting, coarse failure causes, receiver-selected
raw SER/BER, oracle debug SER/BER, PER, and accepted-payload BER. Use
`rx_raw_ser`, `rx_raw_ber`, and `per` for quality claims. The `oracle_raw_*`
columns are only a diagnostic for "could a better candidate selection have
helped?". Timing-search diagnostics report the selected profile, how many
symbols used full/local/center search, and the average timing offsets checked
per decoded symbol. Receiver diagnostics also report a linear clock fit from
known preamble/sync/pilot positions as `estimated_clock_ppm`,
`clock_fit_error_rms_samples`, and `clock_fit_points`. By default this is
observability; with `--adaptive-clock-tracking` the same fitted model may be
used to choose data-symbol extraction positions when the fit is internally
consistent.

The PCM measurement is not yet a calibrated standards-style Eb/N0 compliance
test. It prints estimated Eb/N0 and processing gain from the configured audio
bandwidth and measured frame duration so overhead changes are visible.

`measure-pcm-debug` is a deterministic first-failure tool. It uses the same
seeded payload/channel generation as `measure-pcm` and prints the trial index,
sync score, selected candidate span, estimated sample-rate error, header raw
SER/BER counts, FEC syndrome diagnostics, decoded header bytes before
validation, and whether the oracle raw candidate would have been error-free.

The real PCM receiver now has explicit demodulator modes. `fixed-maxlog`
matches the old fixed-scale max-log LLR sign convention. `calibrated-maxlog`
keeps the same waveform and FEC, but estimates metric noise from receiver-visible
preamble, sync, and pilot symbols, then uses that to reduce overconfident LLR
saturation before BP decoding. Pilots also feed timing/template diagnostics and
are rejected for timing updates when their margin is weak or their timing offset
is an outlier. This does not change occupied bandwidth, required channel width,
symbol count, pilot spacing, or FEC rate.

Historical local metric-model run, `./chirp_modem measure-metric 500`, on
2026-06-13:

| Profile | SNR dB | Raw SER | Raw BER | PER |
|---------|--------|---------|---------|-----|
| awgn-metric | 12 | 0 | 0 | 0 |
| awgn-metric | 9 | 0.00000625 | 0.000003125 | 0 |
| awgn-metric | 6 | 0.0019375 | 0.00100938 | 0 |
| awgn-metric | 3 | 0.0495312 | 0.0263531 | 0.066 |
| awgn-metric | 0 | 0.231387 | 0.123247 | 1 |
| awgn-metric | -3 | 0.469025 | 0.249942 | 1 |
| awgn-metric | -6 | 0.6506 | 0.347298 | 1 |
| radio-metric | 12 | 0.0323437 | 0.0141906 | 0 |
| radio-metric | 9 | 0.0617 | 0.0273938 | 0.098 |
| radio-metric | 6 | 0.174462 | 0.0798219 | 0.954 |
| radio-metric | 3 | 0.381663 | 0.185705 | 1 |
| radio-metric | 0 | 0.5837 | 0.293388 | 1 |
| radio-metric | -3 | 0.722931 | 0.37088 | 1 |
| radio-metric | -6 | 0.805219 | 0.419361 | 1 |

Interpretation:

- In this historical metric-only run, the idealized metric model passed at
  6 dB and began failing around 3 dB.
- In the same historical run, the radio-metric profile began failing around
  9 dB and was mostly failing by 6 dB. Treat that as a regression target and
  model artifact, not as a calibrated RF-channel SNR threshold.
- These SNR values are metric-SNR regression points, not calibrated Eb/N0 curves.
- The apparent gap includes receiver/channel imperfections modeled in that
  synthetic path; it should not be attributed to the BP decoder alone.

### Reducing The Modeled Radio Penalty

The synthetic radio-metric penalty is still a useful design smell. Future work
should reduce it with controlled measurements rather than by claiming standards
performance from the current prototype. Useful next fixes are:

- Calibrate bit LLRs from measured metric noise variance. Current max-log LLRs
  use a conservative fixed scale and clipping, not full likelihoods from an
  estimated channel/noise model.
- Add a preamble-based SFO/CFO estimator. Estimate sample-rate offset and
  residual frequency/phase slope before data, then initialize the timing loop
  from that estimate instead of letting data symbols discover it.
- Tune the new pilot symbols. The current PHY inserts raw symbol `10` every 32
  protected data symbols and uses it for timing/template updates; future work
  should test pilot spacing, pilot sequence choice, and metric-scale estimation.
- Make the interleaver span time and FEC codewords more deliberately. Current
  block interleaving is simple; burst errors from fades/echo timing slips should
  be spread over more codewords.
- Improve the new PRBS scrambler. Current scrambling is applied after FEC and is
  undone by LLR sign flips before BP; future work should evaluate whether
  payload-before-FEC scrambling gives better FEC/interleaver behavior.
- Replace the local LDPC-style matrix with a known short-block LDPC, starting
  with CCSDS `(128,64)` or `(512,256)`. The decoder is now BP-capable, but the
  matrix is still local and not optimized like WiFi/DVB/CCSDS matrices.
- Add normalized/offset min-sum as an option after sum-product. It is often more
  stable with imperfect, non-Gaussian, mis-scaled LLRs and cheaper on small CPUs.
- Improve acquisition scoring to measure preamble slope and multipath energy,
  not only sync-symbol correlation. Reject locks with high side-lobe ambiguity.
- Continue improving the new FFT circular-correlation demodulator toward a true
  complex LoRa-style dechirp detector. The current implementation is faster and
  gives all shift metrics at once, but the waveform is still real PCM rather
  than an analytic complex baseband chirp.
- Measure Eb/N0 and processing gain explicitly. Without a calibrated energy per
  information bit, comparisons to LoRa/WiFi/DVB curves are only qualitative.

### Streaming Acquisition Tests

The built-in chirp selftest includes deterministic streaming tests. They feed a
rolling buffer in chunks, repeatedly call `scan_pcm_window_for_frame`, erase
`discard_prefix_samples`, and verify that the receive buffer remains bounded.

The buffer bound checked in current selftests is less than 10 seconds of PCM:

```text
80,000 samples = 10 seconds = 160,000 PCM bytes
```

### What These Tests Do Not Prove

Current tests do not prove:

- statistically stable raw BER vs SNR,
- statistically stable raw SER vs SNR,
- statistically stable post-FEC BER curves,
- statistically stable FEC BLER curves,
- statistical false alarm rate,
- statistical missed detection rate,
- performance over real RF recordings,
- statistically characterized multipath or fading tolerance,
- Doppler tolerance,
- microphone/speaker nonlinearities,
- FM limiter/de-emphasis/pre-emphasis behavior,
- VOX attack/release behavior,
- adjacent-channel interference,
- long real-recording clock drift.

## Current Regression Tests

### Built-In Chirp Selftest

Run:

```bash
cd lab
./chirp_modem selftest
```

or through Bazel:

```bash
bazel test //lab:chirp_selftest_test
```

The selftest covers bit/byte conversion, Gray mapping, hard and soft
interleavers, FEC structure, selected FEC correction cases, clean modem
roundtrip, leading silence, 0.98x/0.99x/1.01x/1.02x whole-frame time scaling,
streaming acquisition, long idle before a valid frame, no-valid-frame streams,
incomplete frame tails, and rejected incompatible frames.

### Chirp/CSS Roundtrip And Sample-Rate Offset

Fast default scripts:

```bash
bazel test -c opt //lab:chirp_clean_roundtrip_fast_test
bazel test -c opt //lab:chirp_timing_drift_fast_test
```

The older broad diagnostic script still exists as
`lab/chirp_modem_test.sh`, but it is tagged `manual` and `known_failing`
because the 95..105% sweep was already failing on the 95% case before the
fast/slow split. It is useful when working specifically on wide sample-rate
offset behavior:

```bash
bazel test -c opt //lab:chirp_modem_test --test_output=all
```

| Test | Metric | Stream/frame length | Payload size | Impairment model | Impairment location | Impairment amount | Success criterion | Result |
|------|--------|---------------------|--------------|------------------|---------------------|-------------------|-------------------|--------|
| fast chirp clean roundtrip | PER regression | small generated frame | 24 B | none | n/a | none | CRC/protocol pass + exact payload match | default fast test |
| fast chirp whole-frame sample-rate offset | PER regression | small generated frame | 24 B | linear PCM resampling | whole frame | 101% length | CRC/protocol pass + exact payload match | default fast test |
| broad chirp whole-frame sample-rate offset | PER diagnostic | 59,296 PCM bytes = 29,648 samples = 3.706 s before resampling | 30 B | linear PCM resampling | whole frame | 95%, 96%, 97%, 98%, 99%, 101%, 102%, 103%, 104%, 105% length | CRC/protocol pass + exact payload match | manual known-failing diagnostic |
| broad chirp regional sample-rate offset | PER diagnostic | 59,296 PCM bytes = 29,648 samples = 3.706 s before resampling | 30 B | linear PCM resampling | middle 25% of frame | 101% length | CRC/protocol pass + exact payload match | manual diagnostic |

### Chirp/CSS Burst Overwrite Regression

Script: `lab/chirp_corruption_test.sh`

This uses a 256-byte deterministic payload and overwrites selected PCM byte
ranges with `0xff`.

| Test | Metric | Stream/frame length | Payload size | Impairment model | Impairment location | Impairment amount | Success criterion | Result |
|------|--------|---------------------|--------------|------------------|---------------------|-------------------|-------------------|--------|
| chirp preamble overwrite | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | middle of preamble | 16 PCM bytes = 8 samples = 1 ms = 0.0625 symbol | CRC/protocol pass + exact payload match | pass in current regression |
| chirp sync overwrite | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | sync start | 4 PCM bytes = 2 samples = 0.25 ms = 0.015625 symbol | CRC/protocol pass + exact payload match | pass in current regression |
| chirp header-area overwrite | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | first protected data byte after sync | 16 PCM bytes = 8 samples = 1 ms = 0.0625 symbol | CRC/protocol pass + exact payload match | pass in current regression |
| chirp data overwrite | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | protected data region, offset `data_start + 4096` bytes | 16 PCM bytes = 8 samples = 1 ms = 0.0625 symbol | CRC/protocol pass + exact payload match | pass in current regression |
| chirp larger data overwrite | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | protected data region, offset `data_start + 8192` bytes | 64 PCM bytes = 32 samples = 4 ms = 0.25 symbol | CRC/protocol pass + exact payload match | pass in current regression |

### Chirp/CSS FEC Limits And Performance Matrix

Scripts:

```bash
bazel test //lab:chirp_fec_limits_test
bazel test //lab:chirp_fec_performance_test --test_output=all
```

`chirp_fec_limits_test.sh` verifies 1, 4, 8, 16, 32, 64, and 128 PCM-byte
mid-data overwrites for a 256-byte payload. It also checks 16 PCM-byte
overwrites at the data start, at `data_start + 2048`, and at mid-data.

`chirp_fec_performance_test.sh` generates payloads of 4, 8, 16, 32, 64, 128,
256, 512, and 1024 bytes. It reports the largest tested overwrite that still
passes for selected regions. The script currently tests preamble bursts of 16
and 128 PCM bytes, header-region bursts of 16 PCM bytes, and mid-data bursts of
16 and 128 PCM bytes.

Recent generated matrix:

| Test | Metric | Stream/frame length | Payload size | Impairment model | Impairment location | Impairment amount | Success criterion | Result |
|------|--------|---------------------|--------------|------------------|---------------------|-------------------|-------------------|--------|
| chirp FEC performance | PER regression | 34,720 PCM bytes = 17,360 samples = 2.17 s | 4 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 42,912 PCM bytes = 21,456 samples = 2.682 s | 8 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 51,104 PCM bytes = 25,552 samples = 3.194 s | 16 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 67,488 PCM bytes = 33,744 samples = 4.218 s | 32 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 100,256 PCM bytes = 50,128 samples = 6.266 s | 64 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 165,792 PCM bytes = 82,896 samples = 10.362 s | 128 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s | 256 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 559,008 PCM bytes = 279,504 samples = 34.938 s | 512 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |
| chirp FEC performance | PER regression | 1,083,296 PCM bytes = 541,648 samples = 67.706 s | 1024 B | contiguous PCM overwrite with `0xff` | preamble/header/mid-data | preamble 128 B, header 16 B, mid-data 128 B passed in script | CRC/protocol pass + exact payload match | pass |

These are packet recovery regression results, not BER or BLER measurements.

### Chirp/CSS Timing Drift / Sample-Rate Offset Regression

Script:

```bash
bazel test //lab:chirp_timing_drift_test --test_output=all
```

Current routine timing matrix uses payload sizes 4, 8, 16, 32, 64, 128, 256,
and 512 bytes. It tests whole-frame linear resampling to 95%, 100%, and 105% of
the original length. No byte-level bit flips are applied in the current chirp
timing script. Interpret this as a broad sample-rate mismatch/torture matrix,
not a realistic audio-clock ppm drift matrix.

Recent generated matrix:

| Test | Metric | Stream/frame length | Payload size | Impairment model | Impairment location | Impairment amount | Success criterion | Result |
|------|--------|---------------------|--------------|------------------|---------------------|-------------------|-------------------|--------|
| chirp timing drift | PER regression | 34,720 PCM bytes = 17,360 samples = 2.17 s before resampling | 4 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at all three points |
| chirp timing drift | PER regression | 42,912 PCM bytes = 21,456 samples = 2.682 s before resampling | 8 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at all three points |
| chirp timing drift | PER regression | 51,104 PCM bytes = 25,552 samples = 3.194 s before resampling | 16 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at all three points |
| chirp timing drift | PER regression | 67,488 PCM bytes = 33,744 samples = 4.218 s before resampling | 32 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at all three points |
| chirp timing drift | PER regression | 100,256 PCM bytes = 50,128 samples = 6.266 s before resampling | 64 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at all three points |
| chirp timing drift | PER regression | 165,792 PCM bytes = 82,896 samples = 10.362 s before resampling | 128 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at 95% and 100%; fail at 105% |
| chirp timing drift | PER regression | 296,864 PCM bytes = 148,432 samples = 18.554 s before resampling | 256 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at 95% and 100%; fail at 105% |
| chirp timing drift | PER regression | 559,008 PCM bytes = 279,504 samples = 34.938 s before resampling | 512 B | linear PCM resampling | whole frame | 95%, 100%, 105% length | CRC/protocol pass + exact payload match | pass at 95% and 100%; fail at 105% |

The 1024-byte chirp timing matrix is omitted from routine tests because this
broad 95..105% torture sweep is expensive at that size and is not part of the
normal fast development loop.

### Chirp/CSS Synthetic Channel Regression

Scripts:

```bash
bazel test //lab:chirp_audio_channel_impair_test
bazel test //lab:chirp_radio_channel_impair_test
```

| Test | Metric | Payload size | Impairment model | Selected parameters | Success criterion | Result |
|------|--------|--------------|------------------|---------------------|-------------------|--------|
| chirp audio channel impairment | PER regression | 6 B | speaker/recorder/microphone-ish PCM channel | leading/trailing silence, fading gain envelope, DC blocker, low-pass, short echo, deterministic noise | CRC/protocol pass + exact payload match | pass |
| chirp radio channel impairment | PER regression | 6 B | synthetic radio/acoustic PCM channel | 0.3 s leading idle, 1.003x time scale, 0.18-sample wander, slow fade, two echo taps at 19 and 67 samples, deterministic noise and impulses | CRC/protocol pass + exact payload match | pass |

These tests are deterministic packet-recovery regressions. They do not estimate
BER, BLER, SNR margin, or statistical fading performance.

### External Comparison

The receiver now uses the same broad decoder family as common LDPC tooling:
soft-decision belief propagation / message passing. GNU Radio documents its
LDPC decoder as a soft-decision BP decoder for AWGN channels with a supplied
noise variance. That puts the algorithmic class in the right family.

The code construction is not in the same class as current standards:

- WiFi LDPC uses standardized quasi-cyclic LDPC matrices with block lengths
  648, 1296, and 1944 and rates 1/2, 2/3, 3/4, and 5/6.
- DVB-S2/S2X uses standardized BCH outer coding plus LDPC inner coding with
  very large frames and adaptive modulation/coding modes.
- CCSDS short-block LDPC specifies engineered rate-1/2 codes at (128,64),
  (256,128), and (512,256), including published parity-check/generator
  structure and simulated error curves.
- This modem currently uses a local (128,64) systematic sparse code with a
  correct BP decoder, but not an optimized standards matrix, not a randomizer,
  not pilots, and not calibrated LLRs from a known AWGN constellation.

So the current result should be described as: real soft BP decoding for a small
experimental LDPC-style code, useful for modem experiments, but still well below
WiFi/DVB-S2/CCSDS-grade channel coding as a system.

### Streaming / Acquisition Regression

The streaming tests live inside `chirp_modem.cpp` selftest and are run by
`chirp_selftest_test.sh`.

| Test | Metric | Total stream length | Chunk size | Valid frame offset | Distractors | Success criterion | Result |
|------|--------|---------------------|------------|--------------------|-------------|-------------------|--------|
| sliding window stream receiver | acquisition regression | 46,080 samples = 5.76 s = 92,160 PCM bytes | 512 samples | after 6,784 samples = 0.848 s | silence, uniform pseudo-random PCM noise, unrelated chirp bursts | valid payload decoded, distractors ignored, rolling buffer < 80,000 samples | pass |
| long idle then valid frame | acquisition regression | 995,728 samples = 124.466 s = 1,991,456 PCM bytes | 4096 samples | after 961,984 samples = 120.248 s | 120 s of uniform pseudo-random PCM noise, occasional one-symbol unrelated chirp bursts, silence before valid frame | valid payload decoded, rolling buffer < 80,000 samples | pass |
| stream no valid frame | false-accept regression | 106,128 samples = 13.266 s = 212,256 PCM bytes | 4096 samples | none | silence, uniform pseudo-random PCM noise, unrelated chirps, wrong-sync burst, wrong-magic frame, cut-off frame-like fragment | no accepted frame, empty decoded payload, rolling buffer < 80,000 samples | pass |
| stream incomplete frame tail | partial-frame handling | first pass ends mid-frame after 14,688 samples = 1.836 s | 512 samples | after 2,400 samples = 0.3 s | uniform pseudo-random PCM noise before candidate | no decoded frame before completion, `NeedMoreSamples` observed, later appended remainder decodes payload, rolling buffer < 80,000 samples | pass |
| wrong frame rejected | false-accept regression | 64,792 samples = 8.099 s = 129,584 PCM bytes | 512 samples | valid frame after wrong-magic frame and 1,400 silence samples | FEC-valid wrong-magic frame before valid frame | wrong frame rejected, later valid payload decoded, rolling buffer < 80,000 samples | pass |

The deterministic no-valid-frame test passed, but statistical false alarm rate
has not yet been estimated.

### P4 Regression Tests

P4 tests remain in the Bazel suite:

```bash
bazel test //lab:p4modem_roundtrip_test
bazel test //lab:corruption_test
bazel test //lab:fec_limits_test
bazel test //lab:fec_performance_test --test_output=all
bazel test //lab:timing_drift_test --test_output=all
```

Interpret them as regression scripts for the older P4 experiment. Some older
script comments or echo text may use stronger wording than this README. The
current project-level interpretation is packet recovery under specified
synthetic PCM impairments, not a production-readiness claim.

## Repository Layout

```text
lab/BUILD.bazel                 Bazel targets
lab/chirp_modem.cpp             current chirp/CSS modem prototype
lab/p4modem.cpp                 older P4 modem prototype
lab/pcm_impair.cpp              synthetic PCM resampling/bitflip tool
lab/pcm_audio_channel_impair.cpp synthetic speaker/recorder/microphone channel tool
lab/pcm_radio_channel_impair.cpp synthetic echo/fading/noise channel tool
lab/pcm_to_wav.cpp              raw PCM to WAV wrapper
lab/wav_to_pcm.cpp              WAV to raw modem PCM converter
lab/*_test.sh                   Bazel shell regression tests
```

## Current Limitations

- The chirp FEC is experimental and local to this repository.
- The P4 FEC is also local and should not be described as a standards code.
- Current tests are mostly packet success/failure tests.
- Raw BER and raw SER are instrumented in synthetic metric measurements and in
  the PCM measurement path, but current curves are regression aids rather than
  calibrated standards measurements.
- Post-FEC BER is CRC-gated at packet output, and FEC BLER is not separately
  instrumented.
- Statistical false alarm and missed detection rates are not estimated.
- The normal chirp demodulator uses FFT circular correlation; optional
  `--weighted-correlation` currently uses a slower time-domain path.
- Real RF/audio-path behavior is not validated by the current synthetic tests.
- Metric and PCM measurement modes include AWGN-like tests, but not calibrated
  Eb/N0 compliance curves.
- Multipath, fading, clipping, and impulsive noise are present only as simple
  deterministic regression models, not calibrated channel models.
- No FM pre-emphasis/de-emphasis, VOX, or adjacent-channel interference model is
  currently present.

## Roadmap

Possible future measurement tooling:

```text
measure_channel.cpp or measure_channel.py:
  generate many randomized payloads
  encode
  apply controlled impairments:
    - additive Gaussian noise at known SNR
    - uniform PCM noise
    - clipping
    - gain changes
    - sample-rate offset
    - burst overwrite/erasure
    - leading silence and distractors
  instrument receiver:
    - raw symbol decisions
    - raw coded-bit decisions/LLRs
    - FEC decoded bits
    - CRC result
  report:
    - SER
    - raw coded-bit BER
    - post-FEC BER
    - BLER
    - FER/PER
    - false accept rate
    - missed detection rate
    - acquisition latency
```

Other possible engineering work:

- optional faster correlator implementation,
- more systematic acquisition-latency measurement,
- real recorded audio/radio test corpus,
- scrambler/randomizer experiments,
- stronger standards-compatible FEC experiments kept separate from the current
  local code.

## Disclaimer

This repository is an experimental research project and is not intended for
safety-critical communication. Users are responsible for complying with local
radio regulations and licensing requirements.
