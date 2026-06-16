#include "lab/chirp/adaptive_templates.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/modulator.h"

namespace {

void require_close(double actual, double expected, double tolerance) {
    assert(std::abs(actual - expected) <= tolerance);
}

}  // namespace

int main() {
    const std::vector<uint8_t> payload = {0x10, 0x21, 0x32, 0x43, 0x54};
    const std::vector<int16_t> pcm = chirp::modulator::encode_payload_to_pcm(payload);

    const chirp::adaptive::AdaptiveTemplateBank empty =
        chirp::adaptive::build_adaptive_template_bank({}, 0.0,
                                                      chirp::config::NOMINAL_SPAN);
    assert(!empty.valid);

    chirp::adaptive::AdaptiveTemplateBank bank =
        chirp::adaptive::build_adaptive_template_bank(
            pcm, 0.0, chirp::config::NOMINAL_SPAN);
    assert(bank.valid);
    assert(bank.known_symbols >=
           chirp::config::PREAMBLE_SYMBOLS + chirp::config::SYNC_SYMBOLS);
    assert(bank.template_energy > 0.0);

    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        require_close(bank.tpl[0][1][size_t(i)], bank.base[size_t(i)], 1e-9);
    }

    const double old_energy = bank.template_energy;
    chirp::adaptive::update_adaptive_template_bank(
        &bank, pcm, 0.0, chirp::config::NOMINAL_SPAN, 99, 0.25);
    require_close(bank.template_energy, old_energy, 1e-12);

    chirp::adaptive::update_adaptive_template_bank(
        &bank, pcm, chirp::config::NOMINAL_SPAN, chirp::config::NOMINAL_SPAN, 0,
        0.02);
    assert(bank.valid);
    assert(bank.template_energy > 0.0);
    return 0;
}
