#include "lab/chirp/demodulator.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/modulator.h"
#include "lab/chirp/waveform.h"

int main() {
    {
        chirp::demod::SymbolMetrics m;
        for (int s = 0; s < chirp::config::ALPHABET; ++s) {
            m.metric[size_t(s)] = double(s);
        }
        chirp::demodulator::finalize_best_scores(&m);
        assert(m.best_symbol == chirp::config::ALPHABET - 1);
        assert(m.best_score == double(chirp::config::ALPHABET - 1));
        assert(m.second_best_score == double(chirp::config::ALPHABET - 2));
    }
    {
        std::vector<int16_t> pcm;
        chirp::waveform::append_symbol_pcm(pcm, 0);
        pcm.push_back(0);
        chirp::receiver::TimingDiagnostics diag;
        const chirp::demod::SymbolMetrics m =
            chirp::demodulator::decode_symbol_metrics_at(
                pcm, 0.0, chirp::config::NOMINAL_SPAN, false, nullptr, nullptr,
                -1, chirp::receiver::TimingSearchProfile::CenterOnly, &diag);
        assert(m.best_symbol == 0);
        assert(m.best_score > 0.0);
        assert(m.second_best_score <= m.best_score);
        assert(diag.timing_search_center_count == 1);
        assert(diag.timing_search_symbols == 1);
        assert(diag.timing_search_offsets == 1);
        assert(diag.average_offsets_per_symbol == 1.0);
    }
    {
        const std::vector<uint8_t> payload = {0x01, 0x23, 0x45, 0x67};
        const std::vector<int16_t> pcm =
            chirp::modulator::encode_payload_to_pcm(payload);
        const chirp::sync::SyncLock lock =
            chirp::sync::find_sync(pcm, false, nullptr);
        const chirp::adaptive::AdaptiveTemplateBank adaptive =
            chirp::adaptive::build_adaptive_template_bank(
                pcm, lock.preamble_pos, lock.symbol_span);
        assert(adaptive.valid);
        const chirp::demod::MetricStats stats =
            chirp::demodulator::estimate_metric_stats_from_known_symbols(
                pcm, lock, &adaptive, nullptr);
        assert(stats.samples > 0);
        assert(stats.mean_peak_margin > 0.0);
        const double delta =
            chirp::demodulator::ideal_vs_adaptive_template_score_delta(
                pcm, lock, adaptive);
        assert(std::isfinite(delta));
    }
    return 0;
}
