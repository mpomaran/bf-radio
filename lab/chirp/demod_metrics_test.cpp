#include "lab/chirp/demod_metrics.h"

#include <cassert>
#include <cmath>
#include <string>

#include "lab/chirp/bit_utils.h"
#include "lab/chirp/config.h"

int main() {
    for (int raw_symbol = 0; raw_symbol < chirp::config::ALPHABET; ++raw_symbol) {
        chirp::demod::SymbolMetrics m;
        m.metric.fill(-1.0);
        m.metric[size_t(raw_symbol)] = 2.0;
        const uint8_t binary = chirp::bits::gray_to_binary4(uint8_t(raw_symbol));
        const std::array<double, chirp::config::BITS_PER_SYMBOL> llr =
            chirp::demod::symbol_metrics_to_llr(
                m, chirp::demod::fixed_llr_demod_config(), nullptr);
        for (int bit = 0; bit < chirp::config::BITS_PER_SYMBOL; ++bit) {
            const int value = (binary >> (chirp::config::BITS_PER_SYMBOL - 1 - bit)) & 1;
            assert(value == 0 ? llr[size_t(bit)] > 0.0 : llr[size_t(bit)] < 0.0);
        }
    }

    chirp::demod::SymbolMetrics m;
    m.metric.fill(-0.25);
    m.metric[0] = 1.0;
    chirp::demod::MetricStats stats;
    chirp::demod::metric_stats_observe_known_symbol(&stats, m, 0);
    assert(stats.samples == 1);
    assert(stats.loser_variance > 0.0);

    const std::array<double, chirp::config::BITS_PER_SYMBOL> llr =
        chirp::demod::symbol_metrics_to_llr(
            m, chirp::demod::calibrated_llr_demod_config(), &stats);
    for (double v : llr) assert(std::isfinite(v));
    assert(chirp::demod::llr_mode_name(chirp::demod::calibrated_llr_demod_config()) ==
           std::string("calibrated-maxlog"));
    return 0;
}
