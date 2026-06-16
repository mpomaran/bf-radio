// sample_view.h
//
// Small PCM sample extraction helpers used by the chirp receiver. These
// functions intentionally contain no synchronization or demodulation policy.

#ifndef BF_RADIO_LAB_CHIRP_SAMPLE_VIEW_H_
#define BF_RADIO_LAB_CHIRP_SAMPLE_VIEW_H_

#include <array>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

namespace chirp {
namespace sample {

double sample_at(const std::vector<int16_t>& pcm, double pos);

std::array<double, config::SYMBOL_SAMPLES> normalized_symbol_samples(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span);

void normalize_template(std::array<double, config::SYMBOL_SAMPLES>* samples);

double cyclic_array_sample(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    double p);

double corr_score(const std::vector<int16_t>& pcm,
                  double pos,
                  double symbol_span,
                  const std::vector<double>& tpl);

}  // namespace sample
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_SAMPLE_VIEW_H_
