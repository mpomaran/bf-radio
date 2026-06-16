#include "lab/chirp/weighted_correlation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "lab/chirp/sample_view.h"
#include "lab/chirp/waveform.h"

namespace chirp {
namespace weighted {
namespace {

using config::ALPHABET;
using config::PREAMBLE_SYMBOLS;
using config::SYMBOL_SAMPLES;
using config::SYNC_SYMBOLS;

void weighted_model_accumulate_known_symbol(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span,
    int raw_symbol,
    const std::array<double, SYMBOL_SAMPLES>& reference_base,
    std::array<double, SYMBOL_SAMPLES>* residual_sum,
    int* used) {
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
        dot += candidate[size_t(n)] * reference_base[size_t(n)];
    }
    if (dot < 0.0) {
        for (double& v : candidate) v = -v;
    }

    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        const double r = candidate[size_t(n)] - reference_base[size_t(n)];
        (*residual_sum)[size_t(n)] += r * r;
    }
    ++(*used);
}

}  // namespace

WeightedCorrelationModel::WeightedCorrelationModel()
    : weights(), valid(false), known_symbols(0), weight_min(1.0),
      weight_max(1.0), weight_mean(1.0) {
    weights.fill(1.0);
}

double corr_score_adaptive(const std::vector<int16_t>& pcm,
                           double pos,
                           double symbol_span,
                           const std::array<double, SYMBOL_SAMPLES>& tpl) {
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return 0.0;

    const std::array<double, SYMBOL_SAMPLES> samples =
        sample::normalized_symbol_samples(pcm, pos, symbol_span);
    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double a = samples[size_t(i)];
        const double b = tpl[size_t(i)];
        dot += a * b;
        e1 += a * a;
        e2 += b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

double corr_score_weighted(const std::vector<int16_t>& pcm,
                           double pos,
                           double symbol_span,
                           const std::vector<double>& tpl,
                           const WeightedCorrelationModel* weights) {
    if (weights == nullptr || !weights->valid) {
        return sample::corr_score(pcm, pos, symbol_span, tpl);
    }
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return 0.0;

    const std::array<double, SYMBOL_SAMPLES> samples =
        sample::normalized_symbol_samples(pcm, pos, symbol_span);
    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double w = weights->weights[size_t(i)];
        const double a = samples[size_t(i)];
        const double b = tpl[size_t(i)];
        dot += w * a * b;
        e1 += w * a * a;
        e2 += w * b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

double corr_score_adaptive_weighted(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span,
    const std::array<double, SYMBOL_SAMPLES>& tpl,
    const WeightedCorrelationModel* weights) {
    if (weights == nullptr || !weights->valid) {
        return corr_score_adaptive(pcm, pos, symbol_span, tpl);
    }
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return 0.0;

    const std::array<double, SYMBOL_SAMPLES> samples =
        sample::normalized_symbol_samples(pcm, pos, symbol_span);
    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double w = weights->weights[size_t(i)];
        const double a = samples[size_t(i)];
        const double b = tpl[size_t(i)];
        dot += w * a * b;
        e1 += w * a * a;
        e2 += w * b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

WeightedCorrelationModel build_weighted_correlation_model(
    const std::vector<int16_t>& pcm,
    double preamble_pos,
    double sync_pos,
    double symbol_span,
    bool weighted_correlation_enabled,
    const adaptive::AdaptiveTemplateBank* adaptive) {
    WeightedCorrelationModel model;
    if (!weighted_correlation_enabled) return model;

    std::array<double, SYMBOL_SAMPLES> reference_base =
        adaptive != nullptr && adaptive->valid ? adaptive->base
                                               : waveform::ideal_base_template_array();
    sample::normalize_template(&reference_base);

    std::array<double, SYMBOL_SAMPLES> residual_sum = {};
    int used = 0;
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int i = 0; i < PREAMBLE_SYMBOLS; i += 3) {
        weighted_model_accumulate_known_symbol(
            pcm, preamble_pos + i * symbol_span, symbol_span, 0,
            reference_base, &residual_sum, &used);
    }
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        weighted_model_accumulate_known_symbol(
            pcm, sync_pos + i * symbol_span, symbol_span, sync[i],
            reference_base, &residual_sum, &used);
    }

    if (used < 6) return model;

    std::vector<double> residuals;
    residuals.reserve(SYMBOL_SAMPLES);
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        residuals.push_back(residual_sum[size_t(i)] / double(used));
    }
    std::sort(residuals.begin(), residuals.end());
    const double median_residual = residuals[SYMBOL_SAMPLES / 2];
    const double floor_residual = std::max(1e-4, median_residual * 0.35);

    double sum = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double residual = residual_sum[size_t(i)] / double(used);
        const double raw_weight = (median_residual + floor_residual) /
                                  (residual + floor_residual);
        const double clamped = std::max(0.35, std::min(2.50, raw_weight));
        model.weights[size_t(i)] = clamped;
        sum += clamped;
    }
    if (sum <= 1e-9) return model;

    const double mean = sum / SYMBOL_SAMPLES;
    model.weight_min = std::numeric_limits<double>::infinity();
    model.weight_max = 0.0;
    model.weight_mean = 0.0;
    for (double& w : model.weights) {
        w /= mean;
        model.weight_min = std::min(model.weight_min, w);
        model.weight_max = std::max(model.weight_max, w);
        model.weight_mean += w;
    }
    model.weight_mean /= SYMBOL_SAMPLES;
    model.known_symbols = used;
    model.valid = true;
    return model;
}

}  // namespace weighted
}  // namespace chirp
