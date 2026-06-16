#include "lab/chirp/sample_view.h"

#include <cmath>

namespace chirp {
namespace sample {

using config::SYMBOL_SAMPLES;

double sample_at(const std::vector<int16_t>& pcm, double pos) {
    if (pcm.empty()) return 0.0;
    if (pos <= 0.0) return pcm.front();
    if (pos >= double(pcm.size() - 1)) return pcm.back();
    const size_t i = size_t(pos);
    const double frac = pos - double(i);
    return double(pcm[i]) + (double(pcm[i + 1]) - double(pcm[i])) * frac;
}

std::array<double, SYMBOL_SAMPLES> normalized_symbol_samples(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span) {
    std::array<double, SYMBOL_SAMPLES> samples = {};
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return samples;

    double mean = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double p = pos + double(i) * symbol_span / SYMBOL_SAMPLES;
        samples[size_t(i)] = sample_at(pcm, p);
        mean += samples[size_t(i)];
    }
    mean /= SYMBOL_SAMPLES;

    double energy = 0.0;
    for (double& v : samples) {
        v -= mean;
        energy += v * v;
    }
    const double rms = std::sqrt(energy / SYMBOL_SAMPLES);
    if (rms <= 1e-9) {
        samples.fill(0.0);
        return samples;
    }
    for (double& v : samples) v /= rms;
    return samples;
}

void normalize_template(std::array<double, SYMBOL_SAMPLES>* samples) {
    double mean = 0.0;
    for (double v : *samples) mean += v;
    mean /= SYMBOL_SAMPLES;

    double energy = 0.0;
    for (double& v : *samples) {
        v -= mean;
        energy += v * v;
    }
    const double rms = std::sqrt(energy / SYMBOL_SAMPLES);
    if (rms <= 1e-9) {
        samples->fill(0.0);
        return;
    }
    for (double& v : *samples) v /= rms;
}

double cyclic_array_sample(const std::array<double, SYMBOL_SAMPLES>& samples,
                           double p) {
    while (p < 0.0) p += SYMBOL_SAMPLES;
    while (p >= SYMBOL_SAMPLES) p -= SYMBOL_SAMPLES;
    const int i0 = int(std::floor(p));
    const int i1 = (i0 + 1) % SYMBOL_SAMPLES;
    const double frac = p - double(i0);
    return samples[size_t(i0)] * (1.0 - frac) + samples[size_t(i1)] * frac;
}

double corr_score(const std::vector<int16_t>& pcm,
                  double pos,
                  double symbol_span,
                  const std::vector<double>& tpl) {
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return 0.0;

    double mean = 0.0;
    std::array<double, SYMBOL_SAMPLES> samples = {};
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double p = pos + double(i) * symbol_span / SYMBOL_SAMPLES;
        samples[size_t(i)] = sample_at(pcm, p);
        mean += samples[size_t(i)];
    }
    mean /= SYMBOL_SAMPLES;

    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double a = samples[size_t(i)] - mean;
        const double b = tpl[size_t(i)] * 32767.0;
        dot += a * b;
        e1 += a * a;
        e2 += b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

}  // namespace sample
}  // namespace chirp
