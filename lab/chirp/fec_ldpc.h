// fec_ldpc.h
//
// Experimental short-block LDPC-style FEC for the chirp modem. This module owns
// the sparse parity-check code, deterministic encoder, BP decoder, and decode
// diagnostics. It operates on generic bit/LLR vectors and does not parse frames
// or touch PCM/DSP state.

#ifndef BF_RADIO_LAB_CHIRP_FEC_LDPC_H_
#define BF_RADIO_LAB_CHIRP_FEC_LDPC_H_

#include <array>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

namespace chirp {
namespace fec {

struct FecDecodeResult {
    std::vector<uint8_t> bits;
    bool all_blocks_ok;
    int block_count;
    int failed_blocks;
    int max_iterations;
    int max_syndrome_weight;
    int total_syndrome_weight;

    FecDecodeResult();
};

class LDPCCodec {
    static constexpr int K = config::FEC_INFO_BITS;
    static constexpr int P = config::FEC_PARITY_BITS;
    static constexpr int N = config::FEC_CODEWORD_BITS;
    static constexpr int MAX_CHECK_DEGREE = 4;
    static constexpr int MAX_ITER = 40;
    static constexpr double MESSAGE_LIMIT = 18.0;
    static constexpr double TANH_LIMIT = 1.0 - 1e-12;

public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& info_bits);
    static std::vector<uint8_t> decode_from_llr(const std::vector<double>& llr_bits);
    static FecDecodeResult decode_from_llr_result(const std::vector<double>& llr_bits);
    static std::vector<uint8_t> decode_hard(const std::vector<uint8_t>& bits);
    static bool has_unique_nonzero_columns();

private:
    struct ChunkDecodeResult {
        std::array<uint8_t, K> bits;
        bool ok;
        int iterations;
        int syndrome_weight;

        ChunkDecodeResult();
    };

    static uint64_t info_mask(int n);
    static std::array<uint8_t, N> encode_chunk(const std::array<uint8_t, K>& info);
    static int compute_syndrome(const std::array<uint8_t, N>& bits,
                                std::array<uint8_t, P>* syndrome);
    static int positive_mod(int value, int modulus);
    static int append_unique_var(std::array<int, MAX_CHECK_DEGREE>* vars,
                                 int degree,
                                 int bit);
    static std::array<int, MAX_CHECK_DEGREE> check_variables(int row, int* degree);
    static double clamp_message(double value);
    static std::array<uint8_t, N> hard_decision(const std::array<double, N>& llr);
    static ChunkDecodeResult decode_chunk_from_llr(
        const std::array<double, N>& input_llr);
};

std::vector<uint8_t> fec_encode_bits(const std::vector<uint8_t>& info_bits);
std::vector<uint8_t> fec_decode_bits_from_llr(const std::vector<double>& llr_bits);
FecDecodeResult fec_decode_bits_from_llr_result(const std::vector<double>& llr_bits);
std::vector<uint8_t> fec_decode_bits_hard(const std::vector<uint8_t>& bits);

}  // namespace fec
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_FEC_LDPC_H_
