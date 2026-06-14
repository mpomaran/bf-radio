#include "lab/chirp/bit_utils.h"

#include "lab/chirp/config.h"

namespace chirp {
namespace bits {

std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1);
    }
    return bits;
}

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) {
        bytes[i / 8] |= uint8_t(bits[i] & 1) << (7 - int(i % 8));
    }
    return bytes;
}

uint8_t binary_to_gray4(uint8_t x) {
    x &= 0x0F;
    return uint8_t((x ^ (x >> 1)) & 0x0F);
}

uint8_t gray_to_binary4(uint8_t g) {
    g &= 0x0F;
    g ^= uint8_t(g >> 1);
    g ^= uint8_t(g >> 2);
    return uint8_t(g & 0x0F);
}

std::vector<uint8_t> bits_to_symbols(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> symbols;
    symbols.reserve((bits.size() + config::BITS_PER_SYMBOL - 1) / config::BITS_PER_SYMBOL);
    for (size_t i = 0; i < bits.size(); i += config::BITS_PER_SYMBOL) {
        uint8_t binary_symbol = 0;
        for (int j = 0; j < config::BITS_PER_SYMBOL; ++j) {
            binary_symbol <<= 1;
            if (i + size_t(j) < bits.size()) binary_symbol |= bits[i + size_t(j)] & 1;
        }
        symbols.push_back(binary_to_gray4(binary_symbol));
    }
    return symbols;
}

static uint8_t prbs_bit(uint16_t* state) {
    const uint8_t out = uint8_t(*state & 1U);
    const uint16_t feedback =
        uint16_t(((*state >> 0) ^ (*state >> 2) ^ (*state >> 3) ^ (*state >> 5)) & 1U);
    *state = uint16_t((*state >> 1) | (feedback << 15));
    return out;
}

std::vector<uint8_t> scramble_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out;
    out.reserve(bits.size());
    uint16_t state = 0xACE1u;
    for (uint8_t b : bits) out.push_back(uint8_t((b ^ prbs_bit(&state)) & 1U));
    return out;
}

std::vector<double> descramble_llrs(const std::vector<double>& llrs) {
    std::vector<double> out;
    out.reserve(llrs.size());
    uint16_t state = 0xACE1u;
    for (double v : llrs) {
        out.push_back(prbs_bit(&state) ? -v : v);
    }
    return out;
}

}  // namespace bits
}  // namespace chirp
