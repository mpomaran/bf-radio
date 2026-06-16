#include "lab/chirp/demodulator.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "lab/chirp/config.h"
#include "lab/chirp/fft.h"
#include "lab/chirp/sample_view.h"
#include "lab/chirp/timing_tracker.h"
#include "lab/chirp/waveform.h"

namespace chirp {
namespace demodulator {
namespace {

using config::ALPHABET;
using config::PREAMBLE_SYMBOLS;
using config::SYMBOL_SAMPLES;
using config::SYNC_SYMBOLS;
using receiver::TimingDiagnostics;
using receiver::TimingSearchProfile;

bool fast_symbol_metrics_from_base(const std::vector<int16_t>& pcm,
                                   double pos,
                                   double symbol_span,
                                   const dsp::PrecomputedChirpTemplate& base_fft,
                                   const double* fractional_offsets,
                                   int fractional_offset_count,
                                   dsp::CircularCorrelationScratch* scratch,
                                   std::array<double, SYMBOL_SAMPLES>* corr,
                                   demod::SymbolMetrics* m) {
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return false;
    const std::array<double, SYMBOL_SAMPLES> samples =
        sample::normalized_symbol_samples(pcm, pos, symbol_span);
    double energy = 0.0;
    for (double v : samples) energy += v * v;
    if (energy <= 1e-9) return false;

    dsp::circular_chirp_correlation_precomputed_into(samples, base_fft, scratch, corr);
    for (int s = 0; s < ALPHABET; ++s) {
        const double shift = double(s * SYMBOL_SAMPLES / ALPHABET);
        double score = -1.0;
        for (int i = 0; i < fractional_offset_count; ++i) {
            score = std::max(
                score,
                std::abs(dsp::cyclic_corr_sample(*corr, shift + fractional_offsets[i])));
        }
        m->metric[size_t(s)] = score;
    }
    return true;
}

void timing_diag_record_search(TimingDiagnostics* diag,
                               TimingSearchProfile profile,
                               int offset_count) {
    if (diag == nullptr) return;
    switch (profile) {
        case TimingSearchProfile::Full:
            ++diag->timing_search_full_count;
            break;
        case TimingSearchProfile::Local:
            ++diag->timing_search_local_count;
            break;
        case TimingSearchProfile::CenterOnly:
            ++diag->timing_search_center_count;
            break;
    }
    ++diag->timing_search_symbols;
    diag->timing_search_offsets += offset_count;
    diag->average_offsets_per_symbol =
        double(diag->timing_search_offsets) /
        double(std::max(1, diag->timing_search_symbols));
}

}  // namespace

void finalize_best_scores(demod::SymbolMetrics* m) {
    m->best_score = -1.0;
    m->second_best_score = -1.0;
    m->best_symbol = 0;
    for (int s = 0; s < ALPHABET; ++s) {
        const double score = m->metric[size_t(s)];
        if (score > m->best_score) {
            m->second_best_score = m->best_score;
            m->best_score = score;
            m->best_symbol = s;
        } else if (score > m->second_best_score) {
            m->second_best_score = score;
        }
    }
}

demod::SymbolMetrics decode_symbol_metrics_at(
    const std::vector<int16_t>& pcm,
    double pos,
    double symbol_span,
    bool intermediate,
    const adaptive::AdaptiveTemplateBank* adaptive,
    const weighted::WeightedCorrelationModel* weights,
    int rank_symbol,
    TimingSearchProfile search_profile,
    TimingDiagnostics* timing_diag) {
    static const double full_offsets[] = {
        -24.0, -18.0, -12.0, -8.0, -4.0, -2.0, -1.0, -0.5,
        0.0,
        0.5, 1.0, 2.0, 4.0, 8.0, 12.0, 18.0, 24.0
    };
    static const double local_offsets[] = {-2.0, -1.0, 0.0, 1.0, 2.0};
    static const double center_offsets[] = {0.0};

    const double* timing_offsets = full_offsets;
    int timing_offset_count = int(sizeof(full_offsets) / sizeof(full_offsets[0]));
    if (search_profile == TimingSearchProfile::Local) {
        timing_offsets = local_offsets;
        timing_offset_count = int(sizeof(local_offsets) / sizeof(local_offsets[0]));
    } else if (search_profile == TimingSearchProfile::CenterOnly) {
        timing_offsets = center_offsets;
        timing_offset_count = int(sizeof(center_offsets) / sizeof(center_offsets[0]));
    }
    timing_diag_record_search(timing_diag, search_profile, timing_offset_count);

    demod::SymbolMetrics best;
    double best_rank = -1.0;
    dsp::CircularCorrelationScratch corr_scratch;
    std::array<double, SYMBOL_SAMPLES> corr = {};
    for (int offset_index = 0; offset_index < timing_offset_count; ++offset_index) {
        const double timing_offset = timing_offsets[offset_index];
        demod::SymbolMetrics current;
        current.timing_offset = timing_offset;
        bool used_fast = false;
        const bool use_weighted = weights != nullptr && weights->valid;
        if (!use_weighted && adaptive != nullptr && adaptive->valid) {
            const double adaptive_offsets[3] = {-0.35, 0.0, 0.35};
            const double centered_offset[1] = {0.0};
            used_fast = fast_symbol_metrics_from_base(
                pcm, pos + timing_offset, symbol_span, adaptive->base_fft,
                intermediate ? adaptive_offsets : centered_offset,
                intermediate ? 3 : 1, &corr_scratch, &corr, &current);
        } else if (!use_weighted) {
            const double ideal_offsets[3] = {-2.0, 0.0, 2.0};
            const double centered_offset[1] = {0.0};
            used_fast = fast_symbol_metrics_from_base(
                pcm, pos + timing_offset, symbol_span,
                adaptive::ideal_base_precomputed_template(),
                intermediate ? ideal_offsets : centered_offset,
                intermediate ? 3 : 1, &corr_scratch, &corr, &current);
        }

        if (!used_fast) {
            for (int s = 0; s < ALPHABET; ++s) {
                double score = -1.0;
                if (intermediate) {
                    for (int offset_index = 0; offset_index < 3; ++offset_index) {
                        if (adaptive != nullptr && adaptive->valid) {
                            score = std::max(
                                score,
                                weighted::corr_score_adaptive_weighted(
                                    pcm, pos + timing_offset, symbol_span,
                                    adaptive->tpl[size_t(s)][size_t(offset_index)],
                                    weights));
                        } else {
                            score = std::max(
                                score,
                                weighted::corr_score_weighted(
                                    pcm, pos + timing_offset, symbol_span,
                                    waveform::symbol_template(s, offset_index),
                                    weights));
                        }
                    }
                } else {
                    if (adaptive != nullptr && adaptive->valid) {
                        score = weighted::corr_score_adaptive_weighted(
                            pcm, pos + timing_offset, symbol_span,
                            adaptive->tpl[size_t(s)][1], weights);
                    } else {
                        score = weighted::corr_score_weighted(
                            pcm, pos + timing_offset, symbol_span,
                            waveform::symbol_template(s, 1), weights);
                    }
                }
                current.metric[size_t(s)] = score;
            }
        }
        finalize_best_scores(&current);

        const double timing_penalty = (adaptive != nullptr && adaptive->valid) ? 0.012 : 0.003;
        const double rank_score =
            (rank_symbol >= 0 && rank_symbol < ALPHABET)
                ? current.metric[size_t(rank_symbol)]
                : current.best_score;
        const double rank = rank_score - timing_penalty * std::abs(timing_offset);
        if (rank > best_rank) {
            best = current;
            best_rank = rank;
        }
    }
    return best;
}

double known_symbol_template_score(const std::vector<int16_t>& pcm,
                                   const sync::SyncLock& lock,
                                   const adaptive::AdaptiveTemplateBank* adaptive) {
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    double total = 0.0;
    int samples = 0;
    for (int i = 0; i < PREAMBLE_SYMBOLS; i += 6) {
        const demod::SymbolMetrics m =
            decode_symbol_metrics_at(pcm, lock.preamble_pos + i * lock.symbol_span,
                                     lock.symbol_span, true, adaptive, nullptr, 0);
        total += m.metric[0];
        ++samples;
    }
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        const demod::SymbolMetrics m =
            decode_symbol_metrics_at(pcm, lock.sync_pos + i * lock.symbol_span,
                                     lock.symbol_span, true, adaptive, nullptr, sync[i]);
        total += m.metric[size_t(sync[i])];
        ++samples;
    }
    return samples > 0 ? total / double(samples) : 0.0;
}

double ideal_vs_adaptive_template_score_delta(
    const std::vector<int16_t>& pcm,
    const sync::SyncLock& lock,
    const adaptive::AdaptiveTemplateBank& adaptive) {
    if (!adaptive.valid) return 0.0;
    const double adaptive_score = known_symbol_template_score(pcm, lock, &adaptive);
    const double ideal_score = known_symbol_template_score(pcm, lock, nullptr);
    return adaptive_score - ideal_score;
}

demod::MetricStats estimate_metric_stats_from_known_symbols(
    const std::vector<int16_t>& pcm,
    const sync::SyncLock& lock,
    const adaptive::AdaptiveTemplateBank* adaptive,
    const weighted::WeightedCorrelationModel* weights) {
    demod::MetricStats stats;
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int i = 0; i < PREAMBLE_SYMBOLS; i += 4) {
        const demod::SymbolMetrics m =
            decode_symbol_metrics_at(pcm, lock.preamble_pos + i * lock.symbol_span,
                                     lock.symbol_span, true, adaptive, weights, 0);
        demod::metric_stats_observe_known_symbol(&stats, m, 0);
    }
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        const demod::SymbolMetrics m =
            decode_symbol_metrics_at(pcm, lock.sync_pos + i * lock.symbol_span,
                                     lock.symbol_span, true, adaptive, weights, sync[i]);
        demod::metric_stats_observe_known_symbol(&stats, m, sync[i]);
    }
    return stats;
}

}  // namespace demodulator
}  // namespace chirp
