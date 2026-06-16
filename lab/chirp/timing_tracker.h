// timing_tracker.h
//
// Decision-directed timing loop state and small predicates shared by the chirp
// receiver. Higher-level sync acquisition remains in the receiver for now.

#ifndef BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_
#define BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_

namespace chirp {
namespace timing {

struct TimingLoopConfig {
    double data_kp;
    double data_ki;
    double pilot_kp;
    double pilot_ki;
    double max_timing_update;
    double max_span_step;
    double confidence_threshold;
    double pilot_confidence_threshold;

    TimingLoopConfig();
};

struct TimingState {
    double pos;
    double span;
    double timing_error_filtered;

    TimingState(double p, double s);
};

TimingLoopConfig timing_loop_config_for_templates(bool adaptive_templates);

bool pilot_is_strong_for_timing(double margin,
                                double timing_offset,
                                const TimingLoopConfig& cfg);

}  // namespace timing
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_
