#include "lab/chirp/fec_ldpc.h"

#include <algorithm>
#include <cmath>

namespace chirp {
namespace fec {

FecDecodeResult::FecDecodeResult()
    : bits(), all_blocks_ok(true), block_count(0), failed_blocks(0),
      max_iterations(0), max_syndrome_weight(0), total_syndrome_weight(0) {}

LDPCCodec::ChunkDecodeResult::ChunkDecodeResult()
    : bits(), ok(false), iterations(0), syndrome_weight(0) {}

std::vector<uint8_t> LDPCCodec::encode(const std::vector<uint8_t>& info_bits) {
    std::vector<uint8_t> coded;
    coded.reserve(((info_bits.size() + K - 1) / K) * N);
    for (size_t pos = 0; pos < info_bits.size(); pos += K) {
        std::array<uint8_t, K> chunk = {};
        for (int i = 0; i < K && pos + size_t(i) < info_bits.size(); ++i) {
            chunk[size_t(i)] = info_bits[pos + size_t(i)] & 1;
        }
        const auto codeword = encode_chunk(chunk);
        coded.insert(coded.end(), codeword.begin(), codeword.end());
    }
    return coded;
}

std::vector<uint8_t> LDPCCodec::decode_from_llr(const std::vector<double>& llr_bits) {
    return decode_from_llr_result(llr_bits).bits;
}

FecDecodeResult LDPCCodec::decode_from_llr_result(const std::vector<double>& llr_bits) {
    FecDecodeResult result;
    std::vector<uint8_t> decoded;
    decoded.reserve(((llr_bits.size() + N - 1) / N) * K);
    for (size_t pos = 0; pos < llr_bits.size(); pos += N) {
        std::array<double, N> chunk = {};
        for (int i = 0; i < N; ++i) {
            chunk[size_t(i)] = (pos + size_t(i) < llr_bits.size()) ? llr_bits[pos + size_t(i)] : 4.0;
        }
        const auto decoded_chunk = decode_chunk_from_llr(chunk);
        decoded.insert(decoded.end(), decoded_chunk.bits.begin(), decoded_chunk.bits.end());
        ++result.block_count;
        result.max_iterations = std::max(result.max_iterations, decoded_chunk.iterations);
        result.max_syndrome_weight =
            std::max(result.max_syndrome_weight, decoded_chunk.syndrome_weight);
        result.total_syndrome_weight += decoded_chunk.syndrome_weight;
        if (!decoded_chunk.ok) ++result.failed_blocks;
    }
    result.bits = decoded;
    result.all_blocks_ok = result.failed_blocks == 0;
    return result;
}

std::vector<uint8_t> LDPCCodec::decode_hard(const std::vector<uint8_t>& bits) {
    std::vector<double> llr;
    llr.reserve(bits.size());
    for (uint8_t b : bits) llr.push_back((b & 1) ? -4.0 : 4.0);
    return decode_from_llr(llr);
}

bool LDPCCodec::has_unique_nonzero_columns() {
    std::vector<uint64_t> columns;
    columns.reserve(N);
    for (int n = 0; n < K; ++n) columns.push_back(info_mask(n));
    for (int p = 0; p < P; ++p) columns.push_back(uint64_t(1) << p);

    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == 0) return false;
        for (size_t j = i + 1; j < columns.size(); ++j) {
            if (columns[i] == columns[j]) return false;
        }
    }
    return true;
}

uint64_t LDPCCodec::info_mask(int n) {
    const int r0 = n & 63;
    const int r1 = (11 * n + 7) & 63;
    const int r2 = (23 * n + 19) & 63;
    return (uint64_t(1) << r0) | (uint64_t(1) << r1) | (uint64_t(1) << r2);
}

std::array<uint8_t, LDPCCodec::N> LDPCCodec::encode_chunk(
    const std::array<uint8_t, K>& info) {
    std::array<uint8_t, N> codeword = {};
    for (int i = 0; i < K; ++i) codeword[size_t(i)] = info[size_t(i)] & 1;

    for (int p = 0; p < P; ++p) {
        uint8_t parity = 0;
        for (int n = 0; n < K; ++n) {
            if ((info_mask(n) >> p) & 1U) parity ^= info[size_t(n)] & 1;
        }
        codeword[size_t(K + p)] = parity;
    }
    return codeword;
}

int LDPCCodec::compute_syndrome(const std::array<uint8_t, N>& bits,
                                std::array<uint8_t, P>* syndrome) {
    int unsatisfied = 0;
    for (int p = 0; p < P; ++p) {
        uint8_t parity = bits[size_t(K + p)] & 1;
        for (int n = 0; n < K; ++n) {
            if ((info_mask(n) >> p) & 1U) parity ^= bits[size_t(n)] & 1;
        }
        (*syndrome)[size_t(p)] = parity;
        unsatisfied += parity;
    }
    return unsatisfied;
}

int LDPCCodec::positive_mod(int value, int modulus) {
    value %= modulus;
    return value < 0 ? value + modulus : value;
}

int LDPCCodec::append_unique_var(std::array<int, MAX_CHECK_DEGREE>* vars,
                                 int degree,
                                 int bit) {
    for (int i = 0; i < degree; ++i) {
        if ((*vars)[size_t(i)] == bit) return degree;
    }
    (*vars)[size_t(degree)] = bit;
    return degree + 1;
}

std::array<int, LDPCCodec::MAX_CHECK_DEGREE> LDPCCodec::check_variables(
    int row,
    int* degree) {
    std::array<int, MAX_CHECK_DEGREE> vars = {};
    for (int& v : vars) v = -1;
    int deg = 0;
    deg = append_unique_var(&vars, deg, row);
    deg = append_unique_var(&vars, deg, positive_mod(35 * (row - 7), P));
    deg = append_unique_var(&vars, deg, positive_mod(39 * (row - 19), P));
    deg = append_unique_var(&vars, deg, K + row);
    *degree = deg;
    return vars;
}

double LDPCCodec::clamp_message(double value) {
    if (value > MESSAGE_LIMIT) return MESSAGE_LIMIT;
    if (value < -MESSAGE_LIMIT) return -MESSAGE_LIMIT;
    return value;
}

std::array<uint8_t, LDPCCodec::N> LDPCCodec::hard_decision(
    const std::array<double, N>& llr) {
    std::array<uint8_t, N> bits = {};
    for (int i = 0; i < N; ++i) bits[size_t(i)] = llr[size_t(i)] < 0.0 ? 1 : 0;
    return bits;
}

LDPCCodec::ChunkDecodeResult LDPCCodec::decode_chunk_from_llr(
    const std::array<double, N>& input_llr) {
    std::array<double, N> channel_llr = {};
    for (int i = 0; i < N; ++i) {
        channel_llr[size_t(i)] = clamp_message(input_llr[size_t(i)]);
    }

    std::array<std::array<int, MAX_CHECK_DEGREE>, P> vars = {};
    std::array<int, P> check_degree = {};
    std::array<std::array<double, MAX_CHECK_DEGREE>, P> var_to_check = {};
    std::array<std::array<double, MAX_CHECK_DEGREE>, P> check_to_var = {};
    for (int row = 0; row < P; ++row) {
        vars[size_t(row)] = check_variables(row, &check_degree[size_t(row)]);
        for (int edge = 0; edge < check_degree[size_t(row)]; ++edge) {
            var_to_check[size_t(row)][size_t(edge)] =
                channel_llr[size_t(vars[size_t(row)][size_t(edge)])];
        }
    }

    std::array<double, N> posterior = channel_llr;
    std::array<uint8_t, N> bits = hard_decision(posterior);
    std::array<uint8_t, P> syndrome = {};
    int syndrome_weight = compute_syndrome(bits, &syndrome);
    if (syndrome_weight == 0) {
        ChunkDecodeResult result;
        result.ok = true;
        result.iterations = 0;
        result.syndrome_weight = 0;
        for (int i = 0; i < K; ++i) result.bits[size_t(i)] = bits[size_t(i)] & 1;
        return result;
    }

    for (int iter = 0; iter < MAX_ITER; ++iter) {
        for (int row = 0; row < P; ++row) {
            for (int edge = 0; edge < check_degree[size_t(row)]; ++edge) {
                double product = 1.0;
                for (int other = 0; other < check_degree[size_t(row)]; ++other) {
                    if (other == edge) continue;
                    product *= std::tanh(0.5 * var_to_check[size_t(row)][size_t(other)]);
                }
                product = std::max(-TANH_LIMIT, std::min(TANH_LIMIT, product));
                check_to_var[size_t(row)][size_t(edge)] =
                    clamp_message(2.0 * std::atanh(product));
            }
        }

        posterior = channel_llr;
        for (int row = 0; row < P; ++row) {
            for (int edge = 0; edge < check_degree[size_t(row)]; ++edge) {
                const int bit = vars[size_t(row)][size_t(edge)];
                posterior[size_t(bit)] =
                    clamp_message(posterior[size_t(bit)] +
                                  check_to_var[size_t(row)][size_t(edge)]);
            }
        }

        bits = hard_decision(posterior);
        syndrome_weight = compute_syndrome(bits, &syndrome);
        if (syndrome_weight == 0) {
            ChunkDecodeResult result;
            result.ok = true;
            result.iterations = iter + 1;
            result.syndrome_weight = 0;
            for (int i = 0; i < K; ++i) result.bits[size_t(i)] = bits[size_t(i)] & 1;
            return result;
        }

        for (int row = 0; row < P; ++row) {
            for (int edge = 0; edge < check_degree[size_t(row)]; ++edge) {
                const int bit = vars[size_t(row)][size_t(edge)];
                var_to_check[size_t(row)][size_t(edge)] =
                    clamp_message(posterior[size_t(bit)] -
                                  check_to_var[size_t(row)][size_t(edge)]);
            }
        }
    }

    ChunkDecodeResult result;
    result.ok = false;
    result.iterations = MAX_ITER;
    result.syndrome_weight = syndrome_weight;
    for (int i = 0; i < K; ++i) result.bits[size_t(i)] = bits[size_t(i)] & 1;
    return result;
}

std::vector<uint8_t> fec_encode_bits(const std::vector<uint8_t>& info_bits) {
    return LDPCCodec::encode(info_bits);
}

std::vector<uint8_t> fec_decode_bits_from_llr(const std::vector<double>& llr_bits) {
    return LDPCCodec::decode_from_llr(llr_bits);
}

FecDecodeResult fec_decode_bits_from_llr_result(const std::vector<double>& llr_bits) {
    return LDPCCodec::decode_from_llr_result(llr_bits);
}

std::vector<uint8_t> fec_decode_bits_hard(const std::vector<uint8_t>& bits) {
    return LDPCCodec::decode_hard(bits);
}

}  // namespace fec
}  // namespace chirp
