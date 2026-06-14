// interleaver.h
//
// Block interleaver helpers for hard bits and soft LLR values. This module owns
// only the permutation and inverse permutation. It does not know about FEC
// internals, frame parsing, symbols, or PCM.

#ifndef BF_RADIO_LAB_CHIRP_INTERLEAVER_H_
#define BF_RADIO_LAB_CHIRP_INTERLEAVER_H_

#include <cstdint>
#include <vector>

namespace chirp {
namespace interleave {

std::vector<uint8_t> interleave(const std::vector<uint8_t>& in, int columns);
std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in, int columns);
std::vector<double> interleave_soft(const std::vector<double>& in, int columns);
std::vector<double> deinterleave_soft(const std::vector<double>& in, int columns);

std::vector<uint8_t> interleave(const std::vector<uint8_t>& in);
std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in);
std::vector<double> interleave_soft(const std::vector<double>& in);
std::vector<double> deinterleave_soft(const std::vector<double>& in);

}  // namespace interleave
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_INTERLEAVER_H_
