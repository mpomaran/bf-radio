#include "lab/chirp/receiver_options.h"

#include <cassert>
#include <stdexcept>

int main() {
    chirp::receiver::ReceiverOptions defaults;
    assert(defaults.receiver_profile == chirp::receiver::ReceiverProfile::Robust);
    assert(defaults.timing_search_profile == chirp::receiver::TimingSearchProfile::Local);
    assert(defaults.adaptive_llr_enabled);
    assert(defaults.adaptive_clock_tracking_enabled);
    assert(defaults.adaptive_channel_templates_enabled);
    assert(defaults.weighted_correlation_enabled);
    assert(!defaults.rx_diagnostics_enabled);
    assert(!defaults.manual_llr_scale_set);

    chirp::receiver::apply_receiver_profile_defaults(
        &defaults, chirp::receiver::ReceiverProfile::Legacy);
    assert(defaults.receiver_profile == chirp::receiver::ReceiverProfile::Legacy);
    assert(defaults.timing_search_profile == chirp::receiver::TimingSearchProfile::Local);
    assert(!defaults.adaptive_llr_enabled);
    assert(!defaults.adaptive_clock_tracking_enabled);
    assert(!defaults.adaptive_channel_templates_enabled);
    assert(!defaults.weighted_correlation_enabled);

    chirp::receiver::apply_receiver_profile_defaults(
        &defaults, chirp::receiver::ReceiverProfile::Robust);
    defaults.manual_llr_scale = 0.5;
    defaults.manual_llr_scale_set = true;
    const chirp::demod::DemodConfig cfg =
        chirp::receiver::receiver_demod_config(defaults);
    assert(cfg.use_adaptive_llr);
    assert(cfg.llr_scale == 0.5);

    assert(chirp::receiver::parse_receiver_profile("legacy") ==
           chirp::receiver::ReceiverProfile::Legacy);
    assert(chirp::receiver::parse_receiver_profile("robust") ==
           chirp::receiver::ReceiverProfile::Robust);
    assert(chirp::receiver::parse_timing_search_profile("full") ==
           chirp::receiver::TimingSearchProfile::Full);
    assert(chirp::receiver::parse_timing_search_profile("local") ==
           chirp::receiver::TimingSearchProfile::Local);
    assert(chirp::receiver::parse_timing_search_profile("center") ==
           chirp::receiver::TimingSearchProfile::CenterOnly);

    bool threw = false;
    try {
        (void)chirp::receiver::parse_receiver_profile("other");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    return 0;
}
