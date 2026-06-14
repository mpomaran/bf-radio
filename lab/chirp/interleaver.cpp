#include "lab/chirp/interleaver.h"

#include "lab/chirp/config.h"

namespace chirp {
namespace interleave {

std::vector<uint8_t> interleave(const std::vector<uint8_t>& in, int columns) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t idx = row * cols + col;
            if (idx < in.size()) out.push_back(in[idx]);
        }
    }
    return out;
}

std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in, int columns) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<uint8_t> out(in.size(), 0);
    size_t src = 0;
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t dst = row * cols + col;
            if (dst < in.size()) out[dst] = in[src++];
        }
    }
    return out;
}

std::vector<double> interleave_soft(const std::vector<double>& in, int columns) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<double> out;
    out.reserve(in.size());
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t idx = row * cols + col;
            if (idx < in.size()) out.push_back(in[idx]);
        }
    }
    return out;
}

std::vector<double> deinterleave_soft(const std::vector<double>& in, int columns) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<double> out(in.size(), 0.0);
    size_t src = 0;
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t dst = row * cols + col;
            if (dst < in.size()) out[dst] = in[src++];
        }
    }
    return out;
}

std::vector<uint8_t> interleave(const std::vector<uint8_t>& in) {
    return interleave(in, config::FEC_CODEWORD_BITS);
}

std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in) {
    return deinterleave(in, config::FEC_CODEWORD_BITS);
}

std::vector<double> interleave_soft(const std::vector<double>& in) {
    return interleave_soft(in, config::FEC_CODEWORD_BITS);
}

std::vector<double> deinterleave_soft(const std::vector<double>& in) {
    return deinterleave_soft(in, config::FEC_CODEWORD_BITS);
}

}  // namespace interleave
}  // namespace chirp
