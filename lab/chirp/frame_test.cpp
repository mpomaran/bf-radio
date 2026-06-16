#include "lab/chirp/frame.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/modulator.h"

int main() {
    const std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
    const std::vector<uint8_t> frame =
        chirp::frame::build_protected_frame(payload, chirp::config::PROTOCOL_MAGIC,
                                            chirp::config::PROTOCOL_VERSION, 0);

    uint16_t len = 0;
    size_t required_fec_bits = 0;
    assert(chirp::frame::parse_protected_header(frame, &len, &required_fec_bits));
    assert(len == payload.size());
    assert(required_fec_bits >= chirp::config::FEC_CODEWORD_BITS);

    std::vector<uint8_t> decoded;
    assert(chirp::frame::parse_protected_frame(frame, &decoded));
    assert(decoded == payload);

    std::vector<uint8_t> bad_magic = frame;
    bad_magic[0] = 'X';
    assert(!chirp::frame::parse_protected_frame(bad_magic, &decoded));

    std::vector<uint8_t> bad_version = frame;
    bad_version[4] = uint8_t(chirp::config::PROTOCOL_VERSION + 1);
    assert(!chirp::frame::parse_protected_frame(bad_version, &decoded));

    std::vector<uint8_t> bad_crc = frame;
    bad_crc[chirp::config::PROTOCOL_HEADER_BYTES] ^= 0x01;
    assert(!chirp::frame::parse_protected_frame(bad_crc, &decoded));

    const std::vector<uint8_t> tx_bits = chirp::modulator::build_frame_tx_bits(frame);
    std::vector<double> ideal_llrs;
    ideal_llrs.reserve(tx_bits.size());
    for (uint8_t bit : tx_bits) {
        ideal_llrs.push_back((bit & 1U) ? -8.0 : 8.0);
    }
    std::vector<uint8_t> llr_decoded;
    chirp::fec::FecDecodeResult header_fec;
    chirp::fec::FecDecodeResult body_fec;
    assert(chirp::frame::decode_exact_payload_from_llrs(
        ideal_llrs, tx_bits.size(), &llr_decoded, &header_fec, &body_fec));
    assert(llr_decoded == payload);
    assert(header_fec.success);
    assert(body_fec.success);

    bool threw = false;
    try {
        std::vector<uint8_t> too_large(size_t(chirp::config::MAX_PAYLOAD_BYTES) + 1);
        (void)chirp::frame::build_protected_frame(too_large, chirp::config::PROTOCOL_MAGIC,
                                                  chirp::config::PROTOCOL_VERSION, 0);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    return 0;
}
