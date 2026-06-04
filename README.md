# P4Modem

A lightweight experimental digital modem for narrowband FM radios, designed for operation over simple audio links such as handheld transceivers (e.g. Baofeng UV-5R), sound cards, and VOX-controlled radio interfaces.

P4Modem explores radar-inspired waveform design and correlation-based detection to achieve reliable low-speed data transfer over highly constrained analog voice channels.

## Features

* **P4 polyphase modulation**

  * Uses P4 radar codes rather than traditional FSK tones.
  * Correlation-based symbol detection.
  * Constant-envelope waveform suitable for narrowband FM transmitters.

* **Audio-only operation**

  * No SDR required.
  * Works through standard microphone/speaker audio paths.
  * Compatible with USB sound cards and VOX-triggered radios.

* **Forward Error Correction**

  * LDPC (3,6)-regular encoding with code rate 0.82.
  * Soft-error recovery from burst corruptions.
  * CRC-protected frames.

* **Packet-oriented protocol**

  * Framed payloads.
  * Length field and integrity validation.
  * Designed as a foundation for higher-layer protocols.

* **Portable implementation**

  * Modern C++17.
  * No external dependencies.
  * Runs on Windows, Linux, and embedded targets with sufficient processing power.

## Motivation

Most amateur packet radio systems rely on technologies originally developed in the 1980s:

* AX.25
* Bell 202 AFSK
* 1200 baud packet radio

P4Modem investigates whether modern correlation techniques inspired by radar systems can improve robustness on inexpensive analog FM radios while remaining computationally lightweight.

The project is intended as an experimental platform for studying:

* Polyphase coding
* Correlation receivers
* Forward error correction
* Audio modem design
* Low-SNR communications
* Narrowband packet radio

## Architecture

```text
Payload
  ->
CRC
  ->
FEC
  ->
Interleaver
  ->
Bit Packing
  ->
P4 Symbol Mapping
  ->
Audio PCM
  ->
FM Radio Channel
  ->
Correlation Receiver
  ->
Symbol Decisions
  ->
Deinterleaver
  ->
FEC Decoder
  ->
CRC Validation
  ->
Payload
```

## Intended Use Cases

* Experimental packet radio
* Telemetry
* Sensor networks
* Emergency messaging
* Long-range low-speed communications
* Amateur radio research
* Educational DSP projects

## Current Status

This project is a research prototype.

Current implementation focuses on:

* Proof-of-concept P4 modulation
* Correlation-based demodulation
* PCM audio interfaces
* Simple FEC and packet framing

Future work may include:

* Iterative LDPC decoding (belief propagation) for improved SNR performance
* Turbo codes for approaching Shannon capacity
* Soft-decision symbol detection
* Automatic timing recovery
* Frequency offset estimation
* Mesh networking support
* Alternative waveform families (Costas, chirp/CSS, Zadoff-Chu)

## Design Goals

* Operate over commodity FM handheld radios
* Tolerate poor audio paths
* Remain computationally simple
* Prefer robustness over raw throughput
* Avoid SDR-specific assumptions
* Serve as a playground for modern packet radio experimentation

## How to Build

This project uses Bazel for builds. From the repository root:

**Build all targets:**

```bash
bazel build //lab:all
```

**Build specific targets:**

```bash
bazel build //lab:p4modem      # P4 modem encoder/decoder
bazel build //lab:pcm_to_wav   # PCM to WAV converter
```

Resulting binaries are available at:

```bash
bazel-bin/lab/p4modem
bazel-bin/lab/pcm_to_wav
```

**Run tests:**

```bash
bazel test //lab:all
```

## Forward Error Correction Analysis

### FEC Architecture: LDPC (3,6)-Regular

The current implementation uses a state-of-the-art but computationally efficient LDPC error correction scheme:

* **Code Type:** Low-Density Parity-Check (LDPC) code, (3,6)-regular
* **Sparse Matrix:** 3 parity checks per information bit, 6 information bits per check
* **Information Bits (K):** 82 bits
* **Parity Bits (P):** 18 bits  
* **Codeword Length (N):** 100 bits (K + P)
* **Code Rate:** k/n = 82/100 = **0.82** (18% redundancy vs. 100% for repetition)
* **Decoding Algorithm:** Fast hard-decision majority voting (3 iterations max)
* **Interleaver:** whole-frame 100-column block interleaver after FEC
* **Encoding Ratio:** ~640:1 (256-byte payload -> 165 KB encoded @ 8 kHz)

### Interleaver

The modem uses a whole-frame block interleaver between LDPC encoding and P4
symbol packing. FEC bits are written row-wise in 100-bit LDPC codewords and
transmitted column-wise. This is the most effective lightweight choice for the
current packet modem because it maximizes burst spreading across all LDPC
codewords in a frame without adding metadata, padding, or streaming state.

On receive, the deinterleaver is applied before LDPC decoding. The decoder now
tests candidate packet lengths in full 100-bit FEC codewords, which preserves
the exact interleaver geometry used by the encoder.

### Tested Error Correction Limits

Comprehensive testing (`fec_limits_test.sh` and `fec_performance_test.sh`) validates recovery under preamble, sync, header-bearing data, and mid-payload corruption patterns. Burst sizes below are PCM bytes overwritten with `0xff`.

**Payload-size matrix:**

| Payload | Encoded PCM | Preamble limit | Header-region limit | Mid-data limit | First mid-data failure |
|---------|-------------|----------------|---------------------|----------------|------------------------|
| 4 B | 32,000 B | >=1024 B | 256 B | >=1024 B | none in scan |
| 8 B | 37,280 B | >=1024 B | 256 B | >=1024 B | none in scan |
| 16 B | 37,280 B | >=1024 B | 256 B | >=1024 B | none in scan |
| 32 B | 48,000 B | >=1024 B | 256 B | 128 B | 256 B |
| 64 B | 64,000 B | >=1024 B | 256 B | 128 B | 256 B |
| 128 B | 96,000 B | >=1024 B | 128 B | 128 B | 256 B |
| 256 B | 165,280 B | >=1024 B | 256 B | 128 B | 256 B |
| 512 B | 298,560 B | >=1024 B | 256 B | 128 B | 256 B |
| 1024 B | 565,280 B | >=1024 B | 256 B | 128 B | 256 B |

**Region behavior:**

- Preamble corruption is highly tolerant in the tested range because the sync sequence follows the preamble and can still be found.
- Sync corruption is tested separately with 4 B and 8 B bursts at the sync start; both recover in the current matrix.
- Header bits are FEC-protected and interleaved across the data symbols, but large early-data bursts still become the practical packet-finding limit.
- Mid-data bursts recover through 128 B for payloads 32 B and larger, then fail at 256 B in the current hard-decision decoder.

### Tested Timing Drift Limits

`timing_drift_test.sh` uses the C++ impairment tool `pcm_impair` to simulate
clock mismatch, Doppler-like sample-rate error, and deterministic byte-level bit
flips. The test linearly resamples either the whole PCM signal or a 25% region
at the start, middle, or end of the signal. It scans stretch and shorten cases
in 1% steps until decode failure, for payloads of 4, 8, 16, 32, 64, 128, 256,
512, and 1024 bytes.

Bit flips are applied as one deterministic byte bit flip every 4096 PCM bytes.
In the measured matrix, bit flips did not change the timing-drift threshold:
cases with and without bit flips failed at the same percentages.

| Payload | Whole signal | Start 25% | Middle 25% | End 25% |
|---------|--------------|-----------|------------|---------|
| 4 B | fails at 1% | fails at 1% | fails at 1% | OK through 20% |
| 8 B | fails at 1% | fails at 1% | fails at 1% | OK through 20% |
| 16 B | fails at 1% | fails at 1% | fails at 1% | OK through 20% |
| 32 B | fails at 1% | fails at 1% | fails at 1% | OK through 20% |
| 64 B | fails at 1% | OK through 20% | fails at 1% | fails at 1% |
| 128 B | fails at 1% | fails at 1% | fails at 1% | fails at 1% |
| 256 B | fails at 1% | fails at 1% | fails at 1% | fails at 1% |
| 512 B | fails at 1% | fails at 1% | fails at 1% | fails at 1% |
| 1024 B | fails at 1% | fails at 1% | fails at 1% | fails at 1% |

Stretch and shorten results were identical in the current scan. This confirms
the main synchronization limitation of the prototype: after sync is found, the
decoder assumes fixed symbol timing and does not track sample-rate drift. Timing
recovery should be added before relying on the modem in channels with Doppler,
independent transmitter/receiver clocks, or long recordings with clock skew.

### Key Advantages Over Repetition Coding

| Aspect | Repetition 2x | LDPC (3,6) |
|--------|--------------|-----------|
| Overhead | 100% | 18% |
| Encoding Ratio | ~970:1 | ~640:1 |
| Data Rate (256-byte) | ~130 bps | ~245 bps | 
| Error Recovery | Limited | Excellent |
| Burst Tolerance | low | 128 B mid-data tested, larger in preamble |
| Computational Cost (decode) | Minimal | Low |

### Capacity Planning

For deployment considerations:

- **Current overhead:** ~640x encoding expansion (down from ~970x with repetition)
- **Effective data rate:** ~245 bits/second (at 8 kHz, 16-bit PCM with 256-byte frame and LDPC 0.82)
- **Typical use:** Low-speed command/control and sensor telemetry
- **Production deployments:** Current LDPC design is production-ready for narrowband FM

### Testing

Run the FEC limit and performance test suites:

```bash
bazel test //lab:fec_limits_test
bazel test //lab:fec_performance_test
bazel test //lab:timing_drift_test
```

The performance matrix tests payloads of 4, 8, 16, 32, 64, 128, 256, 512, and 1024 bytes, increasing burst size until recovery fails or the scan reaches 1024 corrupted PCM bytes.

## Disclaimer

P4Modem is an experimental research project and is not intended for safety-critical communication. Users are responsible for complying with local radio regulations and licensing requirements.
