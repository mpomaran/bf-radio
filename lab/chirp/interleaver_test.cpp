#include "lab/chirp/interleaver.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    assert(chirp::interleave::interleave(std::vector<uint8_t>()).empty());
    assert(chirp::interleave::deinterleave(std::vector<uint8_t>()).empty());

    std::vector<uint8_t> hard(257);
    for (size_t i = 0; i < hard.size(); ++i) hard[i] = uint8_t(i & 1U);
    assert(chirp::interleave::deinterleave(chirp::interleave::interleave(hard)) == hard);

    std::vector<double> soft(257);
    for (size_t i = 0; i < soft.size(); ++i) soft[i] = double(i) * 0.25 - 12.0;
    assert(chirp::interleave::deinterleave_soft(chirp::interleave::interleave_soft(soft)) == soft);

    const std::vector<uint8_t> short_bits = {1, 0, 1};
    assert(chirp::interleave::deinterleave(chirp::interleave::interleave(short_bits), 8) ==
           short_bits);
    return 0;
}
