// demodulator.h
//
// Chirp symbol demodulation helpers. This module computes per-symbol
// correlation metrics and known-symbol quality estimates; payload/frame decode
// remains in the receiver.

#ifndef BF_RADIO_LAB_CHIRP_DEMODULATOR_H_
#define BF_RADIO_LAB_CHIRP_DEMODULATOR_H_

#include <cstdint>
#include <vector>

#include "lab/chirp/adaptive_templates.h"
#include "lab/chirp/demod_metrics.h"
#include "lab/chirp/receiver_diagnostics.h"
#include "lab/chirp/receiver_options.h"
#include "lab/chirp/sync_acquisition.h"
#include "lab/chirp/weighted_correlation.h"

namespace chirp {
namespace demodulator {

void finalize_best_scores(demod::SymbolMetrics* m);

demod::SymbolMetrics decode_symbol_metrics_at(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span,
    bool intermediate,
    const adaptive::AdaptiveTemplateBank* adaptive = nullptr,
    const weighted::WeightedCorrelationModel* weights = nullptr,
    int rank_symbol = -1,
    receiver::TimingSearchProfile search_profile = receiver::TimingSearchProfile::Full,
    receiver::TimingDiagnostics* timing_diag = nullptr);

double known_symbol_template_score(const std::vector<int16_t>& pcm,
                                   const sync::SyncLock& lock,
                                   const adaptive::AdaptiveTemplateBank* adaptive);

double ideal_vs_adaptive_template_score_delta(
    const std::vector<int16_t>& pcm,
    const sync::SyncLock& lock,
    const adaptive::AdaptiveTemplateBank& adaptive);

demod::MetricStats estimate_metric_stats_from_known_symbols(
    const std::vector<int16_t>& pcm,
    const sync::SyncLock& lock,
    const adaptive::AdaptiveTemplateBank* adaptive,
    const weighted::WeightedCorrelationModel* weights = nullptr);

}  // namespace demodulator
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_DEMODULATOR_H_
