// weighted_correlation.h
//
// Optional time-domain correlation weighting learned from known symbols. The
// model is receiver-side only and does not change the transmitted waveform.

#ifndef BF_RADIO_LAB_CHIRP_WEIGHTED_CORRELATION_H_
#define BF_RADIO_LAB_CHIRP_WEIGHTED_CORRELATION_H_

#include <array>
#include <cstdint>
#include <vector>

#include "lab/chirp/adaptive_templates.h"
#include "lab/chirp/config.h"

namespace chirp {
namespace weighted {

struct WeightedCorrelationModel {
    std::array<double, config::SYMBOL_SAMPLES> weights;
    bool valid;
    int known_symbols;
    double weight_min;
    double weight_max;
    double weight_mean;

    WeightedCorrelationModel();
};

double corr_score_adaptive(const std::vector<int16_t>& pcm,
                           double pos,
                           double symbol_span,
                           const std::array<double, config::SYMBOL_SAMPLES>& tpl);

double corr_score_weighted(const std::vector<int16_t>& pcm,
                           double pos,
                           double symbol_span,
                           const std::vector<double>& tpl,
                           const WeightedCorrelationModel* weights);

double corr_score_adaptive_weighted(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span,
    const std::array<double, config::SYMBOL_SAMPLES>& tpl,
    const WeightedCorrelationModel* weights);

WeightedCorrelationModel build_weighted_correlation_model(
    const std::vector<int16_t>& pcm,
    double preamble_pos,
    double sync_pos,
    double symbol_span,
    bool weighted_correlation_enabled,
    const adaptive::AdaptiveTemplateBank* adaptive);

}  // namespace weighted
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_WEIGHTED_CORRELATION_H_
