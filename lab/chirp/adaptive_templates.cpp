#include "lab/chirp/adaptive_templates.h"

#include <cmath>

#include "lab/chirp/sample_view.h"
#include "lab/chirp/waveform.h"

namespace chirp {
namespace adaptive {

using config::ALPHABET;
using config::PREAMBLE_SYMBOLS;
using config::SYMBOL_SAMPLES;
using config::SYNC_SYMBOLS;

AdaptiveTemplateBank::AdaptiveTemplateBank()
    : base(), base_fft(), tpl(), valid(false), known_symbols(0),
      template_energy(0.0) {}

const dsp::PrecomputedChirpTemplate& ideal_base_precomputed_template() {
    static const dsp::PrecomputedChirpTemplate tpl =
        dsp::make_precomputed_chirp_template(waveform::ideal_base_template_array());
    return tpl;
}

void rebuild_adaptive_templates(AdaptiveTemplateBank* bank) {
    bank->template_energy = 0.0;
    for (double v : bank->base) bank->template_energy += v * v;
    bank->base_fft = dsp::make_precomputed_chirp_template(bank->base);
    const double fractional_offsets[3] = {-0.35, 0.0, 0.35};
    for (int symbol = 0; symbol < ALPHABET; ++symbol) {
        const int shift = symbol * SYMBOL_SAMPLES / ALPHABET;
        for (int offset_index = 0; offset_index < 3; ++offset_index) {
            const double frac = fractional_offsets[offset_index];
            for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
                bank->tpl[size_t(symbol)][size_t(offset_index)][size_t(n)] =
                    sample::cyclic_array_sample(bank->base, double(n + shift) + frac);
            }
            sample::normalize_template(&bank->tpl[size_t(symbol)][size_t(offset_index)]);
        }
    }
}

AdaptiveTemplateBank build_adaptive_template_bank(
    const std::vector<int16_t>& pcm,
    double preamble_pos,
    double symbol_span) {
    AdaptiveTemplateBank bank;
    std::array<double, SYMBOL_SAMPLES> base = {};
    int used = 0;

    for (int sym = 0; sym < PREAMBLE_SYMBOLS; ++sym) {
        std::array<double, SYMBOL_SAMPLES> current =
            sample::normalized_symbol_samples(
                pcm, preamble_pos + sym * symbol_span, symbol_span);
        double energy = 0.0;
        for (double v : current) energy += v * v;
        if (energy <= 1e-9) continue;

        if (used > 0) {
            double dot = 0.0;
            for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
                dot += current[size_t(i)] * base[size_t(i)];
            }
            if (dot < 0.0) {
                for (double& v : current) v = -v;
            }
        }
        for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
            base[size_t(i)] += current[size_t(i)];
        }
        ++used;
    }

    if (used < PREAMBLE_SYMBOLS / 2) return bank;
    sample::normalize_template(&base);
    bank.base = base;
    bank.known_symbols = used;
    rebuild_adaptive_templates(&bank);
    bank.valid = true;

    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        /*
          The sync word is known protocol content, so it can safely refine the
          preamble-learned template before any payload decisions are made.
        */
        update_adaptive_template_bank(
            &bank, pcm, preamble_pos + (PREAMBLE_SYMBOLS + i) * symbol_span,
            symbol_span, sync[i], 0.04);
        ++bank.known_symbols;
    }
    return bank;
}

void update_adaptive_template_bank(AdaptiveTemplateBank* bank,
                                   const std::vector<int16_t>& pcm,
                                   double pos,
                                   double symbol_span,
                                   int raw_symbol,
                                   double learning_rate) {
    if (bank == nullptr || !bank->valid || raw_symbol < 0 || raw_symbol >= ALPHABET) return;
    if (learning_rate <= 0.0) return;

    const std::array<double, SYMBOL_SAMPLES> observed =
        sample::normalized_symbol_samples(pcm, pos, symbol_span);
    double observed_energy = 0.0;
    for (double v : observed) observed_energy += v * v;
    if (observed_energy <= 1e-9) return;

    const int shift = raw_symbol * SYMBOL_SAMPLES / ALPHABET;
    std::array<double, SYMBOL_SAMPLES> candidate = {};
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        candidate[size_t(n)] =
            sample::cyclic_array_sample(observed, double(n - shift));
    }
    sample::normalize_template(&candidate);

    double dot = 0.0;
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        dot += candidate[size_t(n)] * bank->base[size_t(n)];
    }
    if (dot < 0.0) {
        for (double& v : candidate) v = -v;
    }

    const double keep = 1.0 - learning_rate;
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        bank->base[size_t(n)] = keep * bank->base[size_t(n)] +
                                learning_rate * candidate[size_t(n)];
    }
    sample::normalize_template(&bank->base);
    rebuild_adaptive_templates(bank);
}

}  // namespace adaptive
}  // namespace chirp
