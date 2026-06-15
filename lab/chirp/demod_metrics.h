// demod_metrics.h
//
// Receiver metric containers and symbol-to-bit LLR conversion. This module owns
// the small value types used by demodulation accounting. It does not acquire
// sync, track timing, or compute chirp correlations from PCM samples.

#ifndef BF_RADIO_LAB_CHIRP_DEMOD_METRICS_H_
#define BF_RADIO_LAB_CHIRP_DEMOD_METRICS_H_

#include <array>
#include <vector>

#include "lab/chirp/config.h"

namespace chirp {
namespace demod {

struct SymbolMetrics {
    std::array<double, config::ALPHABET> metric;
    double best_score;
    double second_best_score;
    int best_symbol;
    double timing_offset;

    SymbolMetrics();
};

struct DemodConfig {
    double llr_scale;
    double llr_clip;
    double llr_temperature;
    bool use_logsumexp_llr;
    bool use_noise_variance_llr;
    bool use_adaptive_llr;
    double adaptive_llr_scale;
    double known_symbol_margin_median;
    double known_symbol_margin_p05;
    int known_symbol_count;

    DemodConfig();
};

DemodConfig fixed_llr_demod_config();
DemodConfig calibrated_llr_demod_config();
const char* llr_mode_name(const DemodConfig& cfg);

struct MetricStats {
    double winner_mean;
    double runner_up_mean;
    double loser_mean;
    double loser_variance;
    double mean_peak_margin;
    double margin_min;
    double llr_saturation_rate;
    double llr_mean_abs;
    double llr_max_abs;
    double symbol_error_rate;
    int samples;

    double winner_sum;
    double runner_up_sum;
    double loser_sum;
    double loser_sq_sum;
    double margin_sum;
    double llr_abs_sum;
    int loser_samples;
    int llr_samples;
    int llr_saturated;
    int symbol_errors;
    std::vector<double> margin_samples;

    MetricStats();
};

void metric_stats_observe_known_symbol(MetricStats* stats,
                                       const SymbolMetrics& m,
                                       int expected_symbol);

std::array<double, config::BITS_PER_SYMBOL> symbol_metrics_to_llr(
    const SymbolMetrics& m,
    const DemodConfig& cfg = DemodConfig(),
    MetricStats* stats = nullptr);

}  // namespace demod
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_DEMOD_METRICS_H_
