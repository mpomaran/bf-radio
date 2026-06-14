#include "lab/chirp/modulator.h"

#include <stdexcept>

#include "lab/chirp/bit_utils.h"
#include "lab/chirp/config.h"
#include "lab/chirp/fec_ldpc.h"
#include "lab/chirp/frame.h"
#include "lab/chirp/interleaver.h"
#include "lab/chirp/waveform.h"

namespace chirp {
namespace modulator {

size_t pilot_count_for_data_symbols(size_t data_symbols) {
    if (data_symbols == 0) return 0;
    return (data_symbols - 1) / size_t(config::PILOT_INTERVAL_SYMBOLS);
}

std::vector<uint8_t> insert_pilot_symbols(const std::vector<uint8_t>& data_symbols) {
    std::vector<uint8_t> out;
    out.reserve(data_symbols.size() + pilot_count_for_data_symbols(data_symbols.size()));
    for (size_t i = 0; i < data_symbols.size(); ++i) {
        if (i > 0 && (i % size_t(config::PILOT_INTERVAL_SYMBOLS)) == 0) {
            out.push_back(uint8_t(config::PILOT_SYMBOL));
        }
        out.push_back(data_symbols[i]);
    }
    return out;
}

std::vector<uint8_t> build_frame_tx_bits(const std::vector<uint8_t>& frame) {
    if (frame.size() < config::PROTOCOL_HEADER_BYTES + config::CRC_BYTES) {
        throw std::runtime_error("Protected frame too short");
    }

    std::vector<uint8_t> header(frame.begin(), frame.begin() + config::PROTOCOL_HEADER_BYTES);
    std::vector<uint8_t> body(frame.begin() + config::PROTOCOL_HEADER_BYTES, frame.end());
    const std::vector<uint8_t> header_fec = fec::fec_encode_bits(bits::bytes_to_bits(header));
    const std::vector<uint8_t> body_fec = fec::fec_encode_bits(bits::bytes_to_bits(body));
    const std::vector<uint8_t> body_tx_bits = interleave::interleave(body_fec);

    std::vector<uint8_t> tx_bits;
    tx_bits.reserve(header_fec.size() + body_tx_bits.size());
    tx_bits.insert(tx_bits.end(), header_fec.begin(), header_fec.end());
    tx_bits.insert(tx_bits.end(), body_tx_bits.begin(), body_tx_bits.end());
    return bits::scramble_bits(tx_bits);
}

std::vector<int16_t> encode_frame_bytes_to_pcm(const std::vector<uint8_t>& frame) {
    const std::vector<uint8_t> tx_bits = build_frame_tx_bits(frame);
    const std::vector<uint8_t> symbols =
        insert_pilot_symbols(bits::bits_to_symbols(tx_bits));

    std::vector<int16_t> pcm;
    pcm.reserve((config::PREAMBLE_SYMBOLS + config::SYNC_SYMBOLS + symbols.size()) *
                    config::SYMBOL_SAMPLES +
                config::SAMPLE_RATE / 4);
    for (int i = 0; i < config::PREAMBLE_SYMBOLS; ++i) waveform::append_symbol_pcm(pcm, 0);

    const int sync[config::SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int s : sync) waveform::append_symbol_pcm(pcm, s);
    for (uint8_t s : symbols) waveform::append_symbol_pcm(pcm, s);
    pcm.insert(pcm.end(), config::SAMPLE_RATE / 4, 0);
    return pcm;
}

std::vector<int16_t> encode_payload_to_pcm(const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> frame =
        frame::build_protected_frame(payload, config::PROTOCOL_MAGIC, config::PROTOCOL_VERSION, 0);
    return encode_frame_bytes_to_pcm(frame);
}

}  // namespace modulator
}  // namespace chirp
