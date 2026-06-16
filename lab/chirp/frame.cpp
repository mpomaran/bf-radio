#include "lab/chirp/frame.h"

#include <stdexcept>

#include "lab/chirp/bit_utils.h"
#include "lab/chirp/config.h"
#include "lab/chirp/crc16.h"
#include "lab/chirp/interleaver.h"

namespace chirp {
namespace frame {

size_t fec_bits_for_info_bytes(size_t info_bytes) {
    const size_t info_bits = info_bytes * 8;
    return ((info_bits + config::FEC_INFO_BITS - 1) / config::FEC_INFO_BITS) *
           config::FEC_CODEWORD_BITS;
}

std::vector<uint8_t> build_protected_frame(const std::vector<uint8_t>& payload,
                                           const uint8_t magic[4],
                                           uint8_t version,
                                           uint8_t flags) {
    if (payload.size() > config::MAX_PAYLOAD_BYTES) {
        throw std::runtime_error("Prototype limit: input max 4096 bytes");
    }

    std::vector<uint8_t> frame;
    frame.push_back(magic[0]);
    frame.push_back(magic[1]);
    frame.push_back(magic[2]);
    frame.push_back(magic[3]);
    frame.push_back(version);
    frame.push_back(uint8_t(payload.size() & 0xFF));
    frame.push_back(uint8_t((payload.size() >> 8) & 0xFF));
    frame.push_back(flags);

    std::vector<uint8_t> crc_input = frame;
    crc_input.insert(crc_input.end(), payload.begin(), payload.end());
    frame.insert(frame.end(), payload.begin(), payload.end());

    const uint16_t crc = crc16_ccitt(crc_input);
    frame.push_back(uint8_t(crc & 0xFF));
    frame.push_back(uint8_t((crc >> 8) & 0xFF));
    return frame;
}

bool parse_protected_header(const std::vector<uint8_t>& bytes,
                            uint16_t* payload_len,
                            size_t* required_fec_bits) {
    if (bytes.size() < config::PROTOCOL_HEADER_BYTES) return false;
    for (int i = 0; i < 4; ++i) {
        if (bytes[size_t(i)] != config::PROTOCOL_MAGIC[i]) return false;
    }
    if (bytes[4] != config::PROTOCOL_VERSION) return false;
    const uint16_t len = uint16_t(bytes[5]) | (uint16_t(bytes[6]) << 8);
    if (len > config::MAX_PAYLOAD_BYTES) return false;
    if (bytes[7] != 0) return false;

    if (payload_len) *payload_len = len;
    if (required_fec_bits) {
        *required_fec_bits =
            config::FEC_CODEWORD_BITS + fec_bits_for_info_bytes(size_t(len) + config::CRC_BYTES);
    }
    return true;
}

bool parse_protected_frame(const std::vector<uint8_t>& bytes,
                           std::vector<uint8_t>* payload) {
    uint16_t len = 0;
    if (!parse_protected_header(bytes, &len, nullptr)) return false;

    const size_t frame_len = config::PROTOCOL_HEADER_BYTES + size_t(len) + config::CRC_BYTES;
    if (bytes.size() < frame_len) return false;
    std::vector<uint8_t> frame(bytes.begin(),
                               bytes.begin() + std::ptrdiff_t(config::PROTOCOL_HEADER_BYTES + len));
    const uint16_t got_crc = uint16_t(bytes[frame_len - 2]) |
                             (uint16_t(bytes[frame_len - 1]) << 8);
    if (got_crc != crc16_ccitt(frame)) return false;
    payload->assign(bytes.begin() + config::PROTOCOL_HEADER_BYTES,
                    bytes.begin() + std::ptrdiff_t(config::PROTOCOL_HEADER_BYTES + len));
    return true;
}

bool decode_exact_payload_from_llrs(const std::vector<double>& llrs,
                                    size_t fec_bit_count,
                                    std::vector<uint8_t>* payload,
                                    fec::FecDecodeResult* header_fec_result,
                                    fec::FecDecodeResult* body_fec_result) {
    if (llrs.size() < fec_bit_count || fec_bit_count < config::FEC_CODEWORD_BITS) {
        return false;
    }

    const std::vector<double> fec_llrs = bits::descramble_llrs(llrs);
    std::vector<double> header_llrs(
        fec_llrs.begin(),
        fec_llrs.begin() + std::ptrdiff_t(config::FEC_CODEWORD_BITS));
    const fec::FecDecodeResult header_result =
        fec::fec_decode_bits_from_llr_result(header_llrs);
    if (header_fec_result) *header_fec_result = header_result;
    const std::vector<uint8_t>& header_bits = header_result.bits;
    std::vector<uint8_t> bytes = bits::bits_to_bytes(header_bits);
    bytes.resize(config::PROTOCOL_HEADER_BYTES);

    size_t required_fec_bits = 0;
    if (!parse_protected_header(bytes, nullptr, &required_fec_bits)) {
        return false;
    }
    if (required_fec_bits != fec_bit_count) return false;

    const size_t body_fec_bits = fec_bit_count - config::FEC_CODEWORD_BITS;
    if (body_fec_bits > 0) {
        std::vector<double> body_tx_llrs(
            fec_llrs.begin() + std::ptrdiff_t(config::FEC_CODEWORD_BITS),
            fec_llrs.begin() + std::ptrdiff_t(fec_bit_count));
        const std::vector<double> body_fec_llrs =
            interleave::deinterleave_soft(body_tx_llrs);
        const fec::FecDecodeResult body_result =
            fec::fec_decode_bits_from_llr_result(body_fec_llrs);
        if (body_fec_result) *body_fec_result = body_result;
        const std::vector<uint8_t>& body_bits = body_result.bits;
        const std::vector<uint8_t> body_bytes = bits::bits_to_bytes(body_bits);
        bytes.insert(bytes.end(), body_bytes.begin(), body_bytes.end());
    } else if (body_fec_result) {
        *body_fec_result = fec::FecDecodeResult();
    }
    return parse_protected_frame(bytes, payload);
}

}  // namespace frame
}  // namespace chirp
