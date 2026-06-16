// adaptive_templates.h
//
// Channel-shaped chirp template bank learned from known symbols. This module
// owns the template representation and update logic, but not symbol decisions.

#ifndef BF_RADIO_LAB_CHIRP_ADAPTIVE_TEMPLATES_H_
#define BF_RADIO_LAB_CHIRP_ADAPTIVE_TEMPLATES_H_

#include <array>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/fft.h"

namespace chirp {
namespace adaptive {

struct AdaptiveTemplateBank {
    std::array<double, config::SYMBOL_SAMPLES> base;
    dsp::PrecomputedChirpTemplate base_fft;
    std::array<std::array<std::array<double, config::SYMBOL_SAMPLES>, 3>,
               config::ALPHABET>
        tpl;
    bool valid;
    int known_symbols;
    double template_energy;

    AdaptiveTemplateBank();
};

const dsp::PrecomputedChirpTemplate& ideal_base_precomputed_template();

void rebuild_adaptive_templates(AdaptiveTemplateBank* bank);

AdaptiveTemplateBank build_adaptive_template_bank(
    const std::vector<int16_t>& pcm,
    double preamble_pos,
    double symbol_span);

void update_adaptive_template_bank(AdaptiveTemplateBank* bank,
                                   const std::vector<int16_t>& pcm,
                                   double pos,
                                   double symbol_span,
                                   int raw_symbol,
                                   double learning_rate);

}  // namespace adaptive
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_ADAPTIVE_TEMPLATES_H_
