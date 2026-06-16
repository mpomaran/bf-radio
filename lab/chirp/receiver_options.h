// receiver_options.h
//
// Receiver-side profile, timing-search, and adaptive feature configuration.
// This module deliberately contains no demodulation or frame-decoding logic.

#ifndef BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_H_
#define BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_H_

#include <string>

#include "lab/chirp/demod_metrics.h"

namespace chirp {
namespace receiver {

enum class TimingSearchProfile {
    Full,
    Local,
    CenterOnly
};

enum class ReceiverProfile {
    Default,
    Legacy,
    Robust
};

struct ReceiverOptions {
    TimingSearchProfile timing_search_profile;
    ReceiverProfile receiver_profile;
    bool rx_diagnostics_enabled;
    bool adaptive_llr_enabled;
    bool adaptive_clock_tracking_enabled;
    bool adaptive_channel_templates_enabled;
    bool weighted_correlation_enabled;
    bool manual_llr_scale_set;
    double manual_llr_scale;

    ReceiverOptions();
};

const char* timing_search_profile_name(TimingSearchProfile profile);
const char* receiver_profile_name(ReceiverProfile profile);

TimingSearchProfile parse_timing_search_profile(const std::string& value);
ReceiverProfile parse_receiver_profile(const std::string& value);

void apply_receiver_profile_defaults(ReceiverOptions* options,
                                     ReceiverProfile profile);

demod::DemodConfig receiver_demod_config(const ReceiverOptions& options);

}  // namespace receiver
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_H_
