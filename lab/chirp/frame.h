// frame.h
//
// Protected CHRP frame construction and validation. This module owns header
// fields, payload length checks, and CRC verification. It does not know about
// PCM, chirp symbols, sync acquisition, or FEC internals beyond encoded length
// accounting.

#ifndef BF_RADIO_LAB_CHIRP_FRAME_H_
#define BF_RADIO_LAB_CHIRP_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lab/chirp/fec_ldpc.h"

namespace chirp {
namespace frame {

size_t fec_bits_for_info_bytes(size_t info_bytes);

std::vector<uint8_t> build_protected_frame(const std::vector<uint8_t>& payload,
                                           const uint8_t magic[4],
                                           uint8_t version,
                                           uint8_t flags);

bool parse_protected_header(const std::vector<uint8_t>& bytes,
                            uint16_t* payload_len,
                            size_t* required_fec_bits);

bool parse_protected_frame(const std::vector<uint8_t>& bytes,
                           std::vector<uint8_t>* payload);

bool decode_exact_payload_from_llrs(const std::vector<double>& llrs,
                                    size_t fec_bit_count,
                                    std::vector<uint8_t>* payload,
                                    fec::FecDecodeResult* header_fec_result = nullptr,
                                    fec::FecDecodeResult* body_fec_result = nullptr);

}  // namespace frame
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_FRAME_H_
