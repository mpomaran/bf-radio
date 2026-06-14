#include "lab/chirp/frame.h"

#include <stdexcept>

#include "lab/chirp/config.h"
#include "lab/chirp/crc16.h"

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

}  // namespace frame
}  // namespace chirp
