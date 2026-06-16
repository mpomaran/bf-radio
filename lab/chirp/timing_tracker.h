// timing_tracker.h
//
// Decision-directed timing loop state and timing diagnostics shared by the
// chirp receiver.

#ifndef BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_
#define BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_

#include "lab/chirp/receiver_diagnostics.h"
#include "lab/chirp/sync_acquisition.h"

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

void timing_diag_record_span(receiver::TimingDiagnostics* diag, double span);

void timing_diag_record_error(receiver::TimingDiagnostics* diag, double error);

void timing_diag_record_clock_point(receiver::TimingDiagnostics* diag,
                                    double expected_sample,
                                    double observed_sample);

void timing_diag_record_sync_clock_points(receiver::TimingDiagnostics* diag,
                                          const sync::SyncLock& lock);

bool timing_diag_has_clock_model(const receiver::TimingDiagnostics* diag);

bool timing_diag_has_tracking_clock_model(const receiver::TimingDiagnostics* diag);

bool timing_diag_clock_tracking_is_safe(const receiver::TimingDiagnostics* diag);

double timing_diag_predict_clock_sample(const receiver::TimingDiagnostics* diag,
                                        double expected_sample);

void timing_diag_record_clock_tracking_error(receiver::TimingDiagnostics* diag,
                                             double before_error,
                                             double after_error);

}  // namespace timing
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_TIMING_TRACKER_H_
