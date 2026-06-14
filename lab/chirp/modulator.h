// modulator.h
//
// Transmit-side bit packing and PCM frame generation. This module owns pilot
// insertion and the protected-frame-to-symbol path. It does not perform file IO
// or receiver-side demodulation.

#ifndef BF_RADIO_LAB_CHIRP_MODULATOR_H_
#define BF_RADIO_LAB_CHIRP_MODULATOR_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chirp {
namespace modulator {

size_t pilot_count_for_data_symbols(size_t data_symbols);
std::vector<uint8_t> insert_pilot_symbols(const std::vector<uint8_t>& data_symbols);
std::vector<uint8_t> build_frame_tx_bits(const std::vector<uint8_t>& frame);
std::vector<int16_t> encode_frame_bytes_to_pcm(const std::vector<uint8_t>& frame);
std::vector<int16_t> encode_payload_to_pcm(const std::vector<uint8_t>& payload);

}  // namespace modulator
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_MODULATOR_H_
