// config.h
//
// Physical and protocol constants for the chirp modem. This module owns the
// fixed PHY/accounting values and protocol identifiers. It deliberately does
// not perform IO, DSP, frame parsing, modulation, or FEC work.

#ifndef BF_RADIO_LAB_CHIRP_CONFIG_H_
#define BF_RADIO_LAB_CHIRP_CONFIG_H_

#include <cstdint>

namespace chirp {
namespace config {

inline constexpr int SAMPLE_RATE = 8000;
inline constexpr int SYMBOL_SAMPLES = 128;
inline constexpr int ALPHABET = 16;
inline constexpr int BITS_PER_SYMBOL = 4;
inline constexpr int PREAMBLE_SYMBOLS = 48;
inline constexpr int SYNC_SYMBOLS = 8;
inline constexpr int PILOT_INTERVAL_SYMBOLS = 32;
inline constexpr int PILOT_SYMBOL = 10;
inline constexpr int FEC_INFO_BITS = 64;
inline constexpr int FEC_PARITY_BITS = 64;
inline constexpr int FEC_CODEWORD_BITS = FEC_INFO_BITS + FEC_PARITY_BITS;
inline constexpr int MAX_PAYLOAD_BYTES = 4096;
inline constexpr int PROTOCOL_HEADER_BYTES = 8;
inline constexpr int CRC_BYTES = 2;
inline constexpr int PHY_VERSION = 1;
inline constexpr uint8_t PROTOCOL_VERSION = 1;
inline constexpr double PI = 3.14159265358979323846;
inline constexpr double FREQ_LOW = 700.0;
inline constexpr double FREQ_HIGH = 2300.0;
inline constexpr double AMP = 0.55;
inline constexpr double NOMINAL_SPAN = double(SYMBOL_SAMPLES);
inline constexpr double STREAM_DECODE_SYNC_SCORE_THRESHOLD = 0.28;
inline constexpr uint8_t PROTOCOL_MAGIC[4] = {'C', 'H', 'R', 'P'};

struct PhyProfile {
    int phy_version;
    int protocol_version;
    int sample_rate;
    int symbol_samples;
    int alphabet;
    int bits_per_symbol;
    int preamble_symbols;
    int sync_symbols;
    int pilot_interval_symbols;
    int pilot_symbol;
    double fec_rate;
    bool legacy_compatible;
    bool same_bitrate_as_legacy;
    bool same_channel_as_legacy;
    double occupied_audio_bandwidth_hz;
    double required_audio_bandwidth_hz;
};

inline PhyProfile current_phy_profile() {
    PhyProfile p;
    p.phy_version = PHY_VERSION;
    p.protocol_version = PROTOCOL_VERSION;
    p.sample_rate = SAMPLE_RATE;
    p.symbol_samples = SYMBOL_SAMPLES;
    p.alphabet = ALPHABET;
    p.bits_per_symbol = BITS_PER_SYMBOL;
    p.preamble_symbols = PREAMBLE_SYMBOLS;
    p.sync_symbols = SYNC_SYMBOLS;
    p.pilot_interval_symbols = PILOT_INTERVAL_SYMBOLS;
    p.pilot_symbol = PILOT_SYMBOL;
    p.fec_rate = double(FEC_INFO_BITS) / double(FEC_CODEWORD_BITS);
    p.legacy_compatible = true;
    p.same_bitrate_as_legacy = true;
    p.same_channel_as_legacy = true;
    p.occupied_audio_bandwidth_hz = FREQ_HIGH - FREQ_LOW;
    p.required_audio_bandwidth_hz = p.occupied_audio_bandwidth_hz + 200.0;
    return p;
}

}  // namespace config
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_CONFIG_H_
