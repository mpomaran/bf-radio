#include "lab/chirp/fec_ldpc.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    assert(chirp::fec::LDPCCodec::has_unique_nonzero_columns());

    std::vector<uint8_t> info(3 * chirp::config::FEC_INFO_BITS);
    for (size_t i = 0; i < info.size(); ++i) info[i] = uint8_t(((i * 7U) + 3U) & 1U);
    const std::vector<uint8_t> coded = chirp::fec::fec_encode_bits(info);

    std::vector<double> llr;
    llr.reserve(coded.size());
    for (uint8_t b : coded) llr.push_back((b & 1) ? -5.0 : 5.0);

    chirp::fec::FecDecodeResult result = chirp::fec::fec_decode_bits_from_llr_result(llr);
    result.bits.resize(info.size());
    assert(result.bits == info);
    assert(result.all_blocks_ok);
    assert(result.block_count == 3);
    assert(result.failed_blocks == 0);

    std::vector<uint8_t> decoded = chirp::fec::fec_decode_bits_hard(coded);
    decoded.resize(info.size());
    assert(decoded == info);
    return 0;
}
