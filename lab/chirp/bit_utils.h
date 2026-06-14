// bit_utils.h
//
// Bit/byte conversion and deterministic bit mapping helpers. This module owns
// byte-to-bit layout, 4-bit Gray mapping, and the PRBS bit scrambler used by
// the modem. It does not parse frames, generate PCM, or perform FEC.

#ifndef BF_RADIO_LAB_CHIRP_BIT_UTILS_H_
#define BF_RADIO_LAB_CHIRP_BIT_UTILS_H_

#include <cstdint>
#include <vector>

namespace chirp {
namespace bits {

std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits);
uint8_t binary_to_gray4(uint8_t x);
uint8_t gray_to_binary4(uint8_t g);
std::vector<uint8_t> bits_to_symbols(const std::vector<uint8_t>& bits);
std::vector<uint8_t> scramble_bits(const std::vector<uint8_t>& bits);
std::vector<double> descramble_llrs(const std::vector<double>& llrs);

}  // namespace bits
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_BIT_UTILS_H_
