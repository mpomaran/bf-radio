#include "lab/chirp/timing_tracker.h"

#include <cassert>
#include <cmath>

#include "lab/chirp/config.h"

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
    {
        chirp::receiver::TimingDiagnostics diag;
        chirp::timing::timing_diag_record_span(&diag, 126.0);
        chirp::timing::timing_diag_record_span(&diag, 130.0);
        assert(diag.span_samples == 2);
        assert(diag.span_estimate_min == 126.0);
        assert(diag.span_estimate_max == 130.0);
        assert(diag.span_estimate_mean == 128.0);

        chirp::timing::timing_diag_record_error(&diag, 3.0);
        chirp::timing::timing_diag_record_error(&diag, 4.0);
        assert(diag.timing_corrections_applied == 2);
        assert(std::abs(diag.timing_error_rms - std::sqrt(12.5)) < 1e-12);
    }
    {
        chirp::receiver::TimingDiagnostics diag;
        chirp::sync::SyncLock lock;
        lock.preamble_pos = 10.0;
        lock.sync_pos = 10.0 + double(chirp::config::PREAMBLE_SYMBOLS) *
                                   chirp::config::NOMINAL_SPAN * 1.001;
        lock.symbol_span = chirp::config::NOMINAL_SPAN * 1.001;
        chirp::timing::timing_diag_record_sync_clock_points(&diag, lock);
        assert(chirp::timing::timing_diag_has_clock_model(&diag));
        assert(std::abs(diag.estimated_clock_ppm - 1000.0) < 1e-6);
        assert(std::abs(chirp::timing::timing_diag_predict_clock_sample(&diag, 100.0) -
                        (diag.clock_offset_samples + diag.clock_scale * 100.0)) <
               1e-12);

        chirp::timing::timing_diag_record_clock_point(&diag, 1000.0, 1011.0);
        assert(chirp::timing::timing_diag_has_tracking_clock_model(&diag));
        chirp::timing::timing_diag_record_clock_tracking_error(&diag, 2.0, 1.0);
        assert(diag.timing_error_compare_samples == 1);
        assert(diag.timing_error_before_rms == 2.0);
        assert(diag.timing_error_after_rms == 1.0);
    }
    return 0;
}
