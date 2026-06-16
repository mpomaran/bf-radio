#include "lab/chirp/sync_acquisition.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/modulator.h"

int main() {
    const std::vector<uint8_t> payload = {0x45, 0x23, 0x11, 0x09, 0xFE};
    const std::vector<int16_t> frame = chirp::modulator::encode_payload_to_pcm(payload);

    const chirp::sync::SyncLock clean = chirp::sync::find_sync(frame, false, nullptr);
    assert(clean.score > 0.14);
    assert(std::abs(clean.preamble_pos) < double(chirp::config::SYMBOL_SAMPLES));
    assert(std::abs(clean.symbol_span - chirp::config::NOMINAL_SPAN) < 8.0);
    assert(chirp::sync::preamble_score_at(frame, clean.preamble_pos,
                                          clean.symbol_span) > 0.14);

    std::vector<int16_t> delayed(2048, 0);
    delayed.insert(delayed.end(), frame.begin(), frame.end());
    const chirp::sync::SyncLock after_silence =
        chirp::sync::find_sync(delayed, false, nullptr);
    assert(after_silence.score > 0.14);
    assert(std::abs(after_silence.preamble_pos - 2048.0) <
           double(chirp::config::SYMBOL_SAMPLES));
    assert(std::abs(after_silence.symbol_span - chirp::config::NOMINAL_SPAN) < 8.0);
    return 0;
}
