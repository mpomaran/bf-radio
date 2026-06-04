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

  * Built-in FEC layer.
  * Interleaving to improve resistance against burst errors.
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
  ↓
CRC
  ↓
FEC
  ↓
Interleaver
  ↓
Bit Packing
  ↓
P4 Symbol Mapping
  ↓
Audio PCM
  ↓
FM Radio Channel
  ↓
Correlation Receiver
  ↓
Symbol Decisions
  ↓
Deinterleaver
  ↓
FEC Decoder
  ↓
CRC Validation
  ↓
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

* Reed-Solomon and LDPC coding
* Adaptive symbol rates
* Automatic timing recovery
* Frequency offset estimation
* Soft-decision decoding
* Mesh networking support
* Alternative waveform families (Costas, chirp/CSS, Zadoff-Chu)

## Design Goals

* Operate over commodity FM handheld radios
* Tolerate poor audio paths
* Remain computationally simple
* Prefer robustness over raw throughput
* Avoid SDR-specific assumptions
* Serve as a playground for modern packet radio experimentation

## Disclaimer

P4Modem is an experimental research project and is not intended for safety-critical communication. Users are responsible for complying with local radio regulations and licensing requirements.
