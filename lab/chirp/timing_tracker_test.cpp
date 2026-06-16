#include "lab/chirp/timing_tracker.h"

#include <cassert>

int main() {
    {
        const chirp::timing::TimingLoopConfig cfg;
        assert(cfg.data_kp == 0.18);
        assert(cfg.data_ki == 0.003);
        assert(cfg.pilot_kp == 0.24);
        assert(cfg.pilot_ki == 0.006);
        assert(cfg.max_timing_update == 4.0);
        assert(cfg.max_span_step == 0.08);
        assert(cfg.confidence_threshold == 0.055);
        assert(cfg.pilot_confidence_threshold == 0.04);
        assert(!chirp::timing::pilot_is_strong_for_timing(0.0, 0.25, cfg));
        assert(chirp::timing::pilot_is_strong_for_timing(
            cfg.pilot_confidence_threshold + 0.1, 0.25, cfg));
        assert(!chirp::timing::pilot_is_strong_for_timing(
            cfg.pilot_confidence_threshold + 0.1,
            cfg.max_timing_update + 1.0, cfg));
    }
    {
        const chirp::timing::TimingLoopConfig cfg =
            chirp::timing::timing_loop_config_for_templates(false);
        assert(cfg.data_kp == 0.45);
        assert(cfg.data_ki == 0.010);
        assert(cfg.pilot_kp == 0.50);
        assert(cfg.pilot_ki == 0.016);
        assert(cfg.max_timing_update == 18.0);
        assert(cfg.max_span_step == 0.08);
        assert(cfg.confidence_threshold == 0.035);
        assert(cfg.pilot_confidence_threshold == 0.025);
    }
    {
        const chirp::timing::TimingState state(12.5, 127.0);
        assert(state.pos == 12.5);
        assert(state.span == 127.0);
        assert(state.timing_error_filtered == 0.0);
    }
    return 0;
}
