#include "lab/chirp/bit_utils.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    const std::vector<uint8_t> bytes = {0x00, 0x5A, 0xC3, 0xFF, 0x10};
    assert(chirp::bits::bits_to_bytes(chirp::bits::bytes_to_bits(bytes)) == bytes);

    for (int i = 0; i < 16; ++i) {
        assert(chirp::bits::gray_to_binary4(chirp::bits::binary_to_gray4(uint8_t(i))) == i);
    }

    std::vector<uint8_t> bits(257);
    for (size_t i = 0; i < bits.size(); ++i) bits[i] = uint8_t((i * 7U + 3U) & 1U);
    const std::vector<uint8_t> scrambled = chirp::bits::scramble_bits(bits);
    assert(chirp::bits::scramble_bits(scrambled) == bits);
    assert(chirp::bits::scramble_bits(bits) == scrambled);

    return 0;
}
