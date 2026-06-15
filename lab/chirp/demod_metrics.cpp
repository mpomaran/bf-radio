#include "lab/chirp/demod_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "lab/chirp/bit_utils.h"

namespace chirp {
namespace demod {

SymbolMetrics::SymbolMetrics()
    : metric(), best_score(-1.0), second_best_score(-1.0),
      best_symbol(0), timing_offset(0.0) {}

DemodConfig::DemodConfig()
    : llr_scale(6.0), llr_clip(8.0), llr_temperature(1.0),
      use_logsumexp_llr(false), use_noise_variance_llr(true),
      use_adaptive_llr(false), adaptive_llr_scale(1.0),
      known_symbol_margin_median(0.0), known_symbol_margin_p05(0.0),
      known_symbol_count(0) {}

DemodConfig fixed_llr_demod_config() {
    DemodConfig cfg;
    cfg.use_noise_variance_llr = false;
    return cfg;
}

DemodConfig calibrated_llr_demod_config() {
    DemodConfig cfg;
    cfg.use_noise_variance_llr = true;
    cfg.llr_scale = 0.50;
    cfg.llr_clip = 8.0;
    return cfg;
}

const char* llr_mode_name(const DemodConfig& cfg) {
    if (cfg.use_logsumexp_llr) return "logsumexp";
    return cfg.use_noise_variance_llr ? "calibrated-maxlog" : "fixed-maxlog";
}

MetricStats::MetricStats()
    : winner_mean(0.0), runner_up_mean(0.0), loser_mean(0.0),
      loser_variance(1.0), mean_peak_margin(0.0),
      margin_min(std::numeric_limits<double>::infinity()),
      llr_saturation_rate(0.0), llr_mean_abs(0.0), llr_max_abs(0.0),
      symbol_error_rate(0.0), samples(0), winner_sum(0.0),
      runner_up_sum(0.0), loser_sum(0.0), loser_sq_sum(0.0),
      margin_sum(0.0), llr_abs_sum(0.0), loser_samples(0),
      llr_samples(0), llr_saturated(0), symbol_errors(0),
      margin_samples() {}

void metric_stats_observe_known_symbol(MetricStats* stats,
                                       const SymbolMetrics& m,
                                       int expected_symbol) {
    if (stats == nullptr || expected_symbol < 0 || expected_symbol >= config::ALPHABET) return;
    double best_loser = -std::numeric_limits<double>::infinity();
    const double winner = m.metric[size_t(expected_symbol)];
    for (int s = 0; s < config::ALPHABET; ++s) {
        if (s == expected_symbol) continue;
        const double loser = m.metric[size_t(s)];
        stats->loser_sum += loser;
        stats->loser_sq_sum += loser * loser;
        ++stats->loser_samples;
        best_loser = std::max(best_loser, loser);
    }
    stats->winner_sum += winner;
    stats->runner_up_sum += best_loser;
    const double margin = winner - best_loser;
    stats->margin_sum += margin;
    stats->margin_min = std::min(stats->margin_min, margin);
    stats->margin_samples.push_back(margin);
    if (m.best_symbol != expected_symbol) ++stats->symbol_errors;
    ++stats->samples;
    stats->winner_mean = stats->winner_sum / std::max(1, stats->samples);
    stats->runner_up_mean = stats->runner_up_sum / std::max(1, stats->samples);
    stats->loser_mean = stats->loser_sum / std::max(1, stats->loser_samples);
    const double loser_second_moment =
        stats->loser_sq_sum / std::max(1, stats->loser_samples);
    stats->loser_variance =
        std::max(1e-6, loser_second_moment - stats->loser_mean * stats->loser_mean);
    stats->mean_peak_margin = stats->margin_sum / std::max(1, stats->samples);
    stats->symbol_error_rate =
        double(stats->symbol_errors) / double(std::max(1, stats->samples));
}

static void metric_stats_observe_llr(MetricStats* stats,
                                     double llr,
                                     const DemodConfig& cfg) {
    if (stats == nullptr) return;
    ++stats->llr_samples;
    const double abs_llr = std::abs(llr);
    stats->llr_abs_sum += abs_llr;
    stats->llr_max_abs = std::max(stats->llr_max_abs, abs_llr);
    stats->llr_mean_abs = stats->llr_abs_sum / double(std::max(1, stats->llr_samples));
    if (abs_llr >= cfg.llr_clip - 1e-9) ++stats->llr_saturated;
    stats->llr_saturation_rate =
        double(stats->llr_saturated) / double(std::max(1, stats->llr_samples));
}

static double logsumexp_metric(const std::array<double, config::ALPHABET>& metric,
                               const std::vector<int>& symbols,
                               double temperature) {
    if (symbols.empty()) return -std::numeric_limits<double>::infinity();
    const double temp = std::max(1e-6, temperature);
    double max_v = -std::numeric_limits<double>::infinity();
    for (int s : symbols) max_v = std::max(max_v, metric[size_t(s)] / temp);
    double sum = 0.0;
    for (int s : symbols) sum += std::exp(metric[size_t(s)] / temp - max_v);
    return temp * (max_v + std::log(std::max(1e-300, sum)));
}

static double clamp_llr(double value, double clip) {
    if (value > clip) return clip;
    if (value < -clip) return -clip;
    return value;
}

std::array<double, config::BITS_PER_SYMBOL> symbol_metrics_to_llr(
    const SymbolMetrics& m,
    const DemodConfig& cfg,
    MetricStats* stats) {
    std::array<double, config::BITS_PER_SYMBOL> llr = {};
    const double variance =
        cfg.use_noise_variance_llr
            ? std::max(0.50, stats != nullptr ? stats->loser_variance : 1.0)
            : 1.0;

    for (int bit = 0; bit < config::BITS_PER_SYMBOL; ++bit) {
        double best0 = -std::numeric_limits<double>::infinity();
        double best1 = -std::numeric_limits<double>::infinity();
        std::vector<int> symbols0;
        std::vector<int> symbols1;
        for (int raw = 0; raw < config::ALPHABET; ++raw) {
            const uint8_t binary_symbol = bits::gray_to_binary4(uint8_t(raw));
            const int value = (binary_symbol >> (config::BITS_PER_SYMBOL - 1 - bit)) & 1;
            if (value == 0) {
                best0 = std::max(best0, m.metric[size_t(raw)]);
                if (cfg.use_logsumexp_llr) symbols0.push_back(raw);
            } else {
                best1 = std::max(best1, m.metric[size_t(raw)]);
                if (cfg.use_logsumexp_llr) symbols1.push_back(raw);
            }
        }
        if (cfg.use_logsumexp_llr) {
            best0 = logsumexp_metric(m.metric, symbols0, cfg.llr_temperature);
            best1 = logsumexp_metric(m.metric, symbols1, cfg.llr_temperature);
        }

        // Positive LLR means binary bit 0 is more likely; negative means bit 1.
        const double effective_scale =
            cfg.llr_scale * (cfg.use_adaptive_llr ? cfg.adaptive_llr_scale : 1.0);
        const double raw = effective_scale * (best0 - best1) / variance;
        llr[size_t(bit)] = clamp_llr(raw, cfg.llr_clip);
        metric_stats_observe_llr(stats, llr[size_t(bit)], cfg);
    }
    return llr;
}

}  // namespace demod
}  // namespace chirp
