#include "lab/chirp/modulator.h"

#include <cassert>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

int main() {
    assert(chirp::modulator::pilot_count_for_data_symbols(0) == 0);
    assert(chirp::modulator::pilot_count_for_data_symbols(1) == 0);
    assert(chirp::modulator::pilot_count_for_data_symbols(
               size_t(chirp::config::PILOT_INTERVAL_SYMBOLS)) == 0);
    assert(chirp::modulator::pilot_count_for_data_symbols(
               size_t(chirp::config::PILOT_INTERVAL_SYMBOLS + 1)) == 1);

    std::vector<uint8_t> symbols(size_t(chirp::config::PILOT_INTERVAL_SYMBOLS + 1), 1);
    const std::vector<uint8_t> with_pilots = chirp::modulator::insert_pilot_symbols(symbols);
    assert(with_pilots.size() == symbols.size() + 1);
    assert(with_pilots[size_t(chirp::config::PILOT_INTERVAL_SYMBOLS)] ==
           uint8_t(chirp::config::PILOT_SYMBOL));

    const std::vector<uint8_t> payload = {0x63, 0x68, 0x69, 0x72, 0x70};
    const std::vector<int16_t> pcm1 = chirp::modulator::encode_payload_to_pcm(payload);
    const std::vector<int16_t> pcm2 = chirp::modulator::encode_payload_to_pcm(payload);
    assert(!pcm1.empty());
    assert(pcm1 == pcm2);
    assert(pcm1.size() > size_t(chirp::config::SAMPLE_RATE / 4));
    return 0;
}
