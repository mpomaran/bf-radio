#include "lab/chirp/weighted_correlation.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/modulator.h"
#include "lab/chirp/sample_view.h"
#include "lab/chirp/waveform.h"

namespace {

void require_close(double actual, double expected, double tolerance) {
    assert(std::abs(actual - expected) <= tolerance);
}

}  // namespace

int main() {
    const std::vector<uint8_t> payload = {0x01, 0x23, 0x45, 0x67};
    const std::vector<int16_t> pcm = chirp::modulator::encode_payload_to_pcm(payload);
    const double preamble_pos = 0.0;
    const double sync_pos =
        double(chirp::config::PREAMBLE_SYMBOLS) * chirp::config::NOMINAL_SPAN;

    const chirp::adaptive::AdaptiveTemplateBank adaptive =
        chirp::adaptive::build_adaptive_template_bank(
            pcm, preamble_pos, chirp::config::NOMINAL_SPAN);
    assert(adaptive.valid);

    const chirp::weighted::WeightedCorrelationModel disabled =
        chirp::weighted::build_weighted_correlation_model(
            pcm, preamble_pos, sync_pos, chirp::config::NOMINAL_SPAN, false,
            &adaptive);
    assert(!disabled.valid);
    assert(disabled.weight_mean == 1.0);

    const chirp::weighted::WeightedCorrelationModel model =
        chirp::weighted::build_weighted_correlation_model(
            pcm, preamble_pos, sync_pos, chirp::config::NOMINAL_SPAN, true,
            &adaptive);
    assert(model.valid);
    assert(model.known_symbols >= 6);
    assert(model.weight_min > 0.0);
    assert(model.weight_max >= model.weight_min);
    require_close(model.weight_mean, 1.0, 1e-12);

    const std::vector<double>& tpl = chirp::waveform::symbol_template(0, 1);
    const double fallback_score =
        chirp::weighted::corr_score_weighted(
            pcm, 0.0, chirp::config::NOMINAL_SPAN, tpl, nullptr);
    const double sample_score =
        chirp::sample::corr_score(pcm, 0.0, chirp::config::NOMINAL_SPAN, tpl);
    require_close(fallback_score, sample_score, 1e-12);

    const double weighted_score =
        chirp::weighted::corr_score_weighted(
            pcm, 0.0, chirp::config::NOMINAL_SPAN, tpl, &model);
    assert(std::isfinite(weighted_score));
    assert(weighted_score >= 0.0);
    return 0;
}
