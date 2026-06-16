#include "lab/chirp/timing_tracker.h"

#include <cmath>

namespace chirp {
namespace timing {

TimingLoopConfig::TimingLoopConfig()
    : data_kp(0.18), data_ki(0.003), pilot_kp(0.24), pilot_ki(0.006),
      max_timing_update(4.0), max_span_step(0.08),
      confidence_threshold(0.055), pilot_confidence_threshold(0.04) {}

TimingState::TimingState(double p, double s)
    : pos(p), span(s), timing_error_filtered(0.0) {}

TimingLoopConfig timing_loop_config_for_templates(bool adaptive_templates) {
    TimingLoopConfig cfg;
    if (!adaptive_templates) {
        cfg.data_kp = 0.45;
        cfg.data_ki = 0.010;
        cfg.pilot_kp = 0.50;
        cfg.pilot_ki = 0.016;
        cfg.max_timing_update = 18.0;
        cfg.confidence_threshold = 0.035;
        cfg.pilot_confidence_threshold = 0.025;
    }
    return cfg;
}

bool pilot_is_strong_for_timing(double margin,
                                double timing_offset,
                                const TimingLoopConfig& cfg) {
    return margin > cfg.pilot_confidence_threshold &&
           std::abs(timing_offset) <= cfg.max_timing_update;
}

}  // namespace timing
}  // namespace chirp
