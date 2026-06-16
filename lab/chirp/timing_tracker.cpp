#include "lab/chirp/timing_tracker.h"

#include <algorithm>
#include <cmath>

#include "lab/chirp/config.h"

namespace chirp {
namespace timing {

using config::NOMINAL_SPAN;
using config::PREAMBLE_SYMBOLS;
using config::SYNC_SYMBOLS;

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

void timing_diag_record_span(receiver::TimingDiagnostics* diag, double span) {
    if (diag == nullptr) return;
    if (diag->span_samples == 0) {
        diag->span_estimate_min = span;
        diag->span_estimate_max = span;
    } else {
        diag->span_estimate_min = std::min(diag->span_estimate_min, span);
        diag->span_estimate_max = std::max(diag->span_estimate_max, span);
    }
    diag->span_sum += span;
    ++diag->span_samples;
    diag->span_estimate_mean = diag->span_sum / double(diag->span_samples);
}

void timing_diag_record_error(receiver::TimingDiagnostics* diag, double error) {
    if (diag == nullptr) return;
    if (diag->timing_corrections_applied == 0) {
        diag->timing_offset_initial_samples = error;
    }
    diag->timing_offset_final_samples = error;
    ++diag->timing_corrections_applied;
    diag->timing_error_sq_sum += error * error;
    ++diag->timing_error_samples;
    diag->timing_error_rms =
        std::sqrt(diag->timing_error_sq_sum / double(diag->timing_error_samples));
}

void timing_diag_record_clock_point(receiver::TimingDiagnostics* diag,
                                    double expected_sample,
                                    double observed_sample) {
    if (diag == nullptr) return;
    ++diag->clock_fit_points;
    diag->clock_fit_sum_expected += expected_sample;
    diag->clock_fit_sum_observed += observed_sample;
    diag->clock_fit_sum_expected_sq += expected_sample * expected_sample;
    diag->clock_fit_sum_observed_sq += observed_sample * observed_sample;
    diag->clock_fit_sum_expected_observed += expected_sample * observed_sample;

    const double n = double(diag->clock_fit_points);
    const double sx = diag->clock_fit_sum_expected;
    const double sy = diag->clock_fit_sum_observed;
    const double sxx = diag->clock_fit_sum_expected_sq;
    const double syy = diag->clock_fit_sum_observed_sq;
    const double sxy = diag->clock_fit_sum_expected_observed;
    const double denom = n * sxx - sx * sx;
    if (diag->clock_fit_points < 2 || std::abs(denom) < 1e-9) return;

    const double scale = (n * sxy - sx * sy) / denom;
    const double offset = (sy - scale * sx) / n;
    const double sse =
        syy + n * offset * offset + scale * scale * sxx +
        2.0 * offset * scale * sx - 2.0 * offset * sy - 2.0 * scale * sxy;
    diag->clock_scale = scale;
    diag->clock_offset_samples = offset;
    diag->estimated_clock_ppm = (scale - 1.0) * 1000000.0;
    diag->clock_fit_error_rms_samples =
        std::sqrt(std::max(0.0, sse / n));
}

void timing_diag_record_sync_clock_points(receiver::TimingDiagnostics* diag,
                                          const sync::SyncLock& lock) {
    timing_diag_record_clock_point(diag, 0.0, lock.preamble_pos);
    timing_diag_record_clock_point(diag,
                                   double(PREAMBLE_SYMBOLS) * NOMINAL_SPAN,
                                   lock.sync_pos);
    timing_diag_record_clock_point(
        diag,
        double(PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * NOMINAL_SPAN,
        lock.sync_pos + double(SYNC_SYMBOLS) * lock.symbol_span);
}

bool timing_diag_has_clock_model(const receiver::TimingDiagnostics* diag) {
    return diag != nullptr && diag->clock_fit_points >= 2 &&
           std::isfinite(diag->clock_scale) &&
           std::isfinite(diag->clock_offset_samples);
}

bool timing_diag_has_tracking_clock_model(const receiver::TimingDiagnostics* diag) {
    return timing_diag_has_clock_model(diag) && diag->clock_fit_points >= 4;
}

bool timing_diag_clock_tracking_is_safe(const receiver::TimingDiagnostics* diag) {
    if (!timing_diag_has_tracking_clock_model(diag)) return false;

    /*
      The fitted clock model is used only when the known-symbol observations are
      internally consistent. In multipath or timing-wander cases a linear fit can
      be worse than the local decision-directed timing loop, so pilot residuals
      gate the optional adaptive clock path.
    */
    if (diag->clock_fit_error_rms_samples > 2.0) return false;
    if (diag->timing_error_compare_samples >= 2 &&
        diag->timing_error_after_rms > diag->timing_error_before_rms * 1.05) {
        return false;
    }
    return true;
}

double timing_diag_predict_clock_sample(const receiver::TimingDiagnostics* diag,
                                        double expected_sample) {
    return diag->clock_offset_samples + diag->clock_scale * expected_sample;
}

void timing_diag_record_clock_tracking_error(receiver::TimingDiagnostics* diag,
                                             double before_error,
                                             double after_error) {
    if (diag == nullptr) return;
    diag->timing_error_before_sq_sum += before_error * before_error;
    diag->timing_error_after_sq_sum += after_error * after_error;
    ++diag->timing_error_compare_samples;
    const double n = double(diag->timing_error_compare_samples);
    diag->timing_error_before_rms = std::sqrt(diag->timing_error_before_sq_sum / n);
    diag->timing_error_after_rms = std::sqrt(diag->timing_error_after_sq_sum / n);
}

}  // namespace timing
}  // namespace chirp
