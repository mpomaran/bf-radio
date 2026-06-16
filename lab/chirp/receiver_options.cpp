#include "lab/chirp/receiver_options.h"

#include <stdexcept>

namespace chirp {
namespace receiver {

ReceiverOptions::ReceiverOptions()
    : timing_search_profile(TimingSearchProfile::Local),
      receiver_profile(ReceiverProfile::Robust),
      rx_diagnostics_enabled(false),
      adaptive_llr_enabled(false),
      adaptive_clock_tracking_enabled(false),
      adaptive_channel_templates_enabled(false),
      weighted_correlation_enabled(false),
      manual_llr_scale_set(false),
      manual_llr_scale(0.0) {
    apply_receiver_profile_defaults(this, receiver_profile);
}

const char* timing_search_profile_name(TimingSearchProfile profile) {
    switch (profile) {
        case TimingSearchProfile::Full: return "full";
        case TimingSearchProfile::Local: return "local";
        case TimingSearchProfile::CenterOnly: return "center";
    }
    return "unknown";
}

const char* receiver_profile_name(ReceiverProfile profile) {
    switch (profile) {
        case ReceiverProfile::Default: return "default";
        case ReceiverProfile::Legacy: return "legacy";
        case ReceiverProfile::Robust: return "robust";
    }
    return "unknown";
}

TimingSearchProfile parse_timing_search_profile(const std::string& value) {
    if (value == "full") return TimingSearchProfile::Full;
    if (value == "local") return TimingSearchProfile::Local;
    if (value == "center") return TimingSearchProfile::CenterOnly;
    throw std::runtime_error("timing search must be full, local, or center");
}

ReceiverProfile parse_receiver_profile(const std::string& value) {
    if (value == "legacy") return ReceiverProfile::Legacy;
    if (value == "robust") return ReceiverProfile::Robust;
    throw std::runtime_error("receiver profile must be legacy or robust");
}

void apply_receiver_profile_defaults(ReceiverOptions* options,
                                     ReceiverProfile profile) {
    options->receiver_profile = profile;
    options->timing_search_profile = TimingSearchProfile::Local;
    if (profile == ReceiverProfile::Legacy) {
        options->adaptive_llr_enabled = false;
        options->adaptive_clock_tracking_enabled = false;
        options->adaptive_channel_templates_enabled = false;
        options->weighted_correlation_enabled = false;
    } else if (profile == ReceiverProfile::Robust) {
        options->adaptive_llr_enabled = true;
        options->adaptive_clock_tracking_enabled = true;
        options->adaptive_channel_templates_enabled = true;
        options->weighted_correlation_enabled = true;
    }
}

demod::DemodConfig receiver_demod_config(const ReceiverOptions& options) {
    demod::DemodConfig cfg = demod::calibrated_llr_demod_config();
    if (options.manual_llr_scale_set) cfg.llr_scale = options.manual_llr_scale;
    cfg.use_adaptive_llr = options.adaptive_llr_enabled;
    return cfg;
}

}  // namespace receiver
}  // namespace chirp
