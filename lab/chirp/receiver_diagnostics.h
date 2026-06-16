// receiver_diagnostics.h
//
// Data-only receiver diagnostics structures. Formatting and transport stay
// outside this module so CLI and tests can choose their own output surface.

#ifndef BF_RADIO_LAB_CHIRP_RECEIVER_DIAGNOSTICS_H_
#define BF_RADIO_LAB_CHIRP_RECEIVER_DIAGNOSTICS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"
#include "lab/chirp/demod_metrics.h"
#include "lab/chirp/fec_ldpc.h"

namespace chirp {
namespace receiver {

struct TimingDiagnostics {
    int pilot_count;
    int pilot_used_for_timing;
    int pilot_rejected_low_confidence;
    double pilot_mean_margin;
    double pilot_timing_offset_rms;
    double timing_error_rms;
    double span_estimate_mean;
    double span_estimate_min;
    double span_estimate_max;
    int timing_search_full_count;
    int timing_search_local_count;
    int timing_search_center_count;
    double average_offsets_per_symbol;
    double timing_offset_initial_samples;
    double timing_offset_final_samples;
    int timing_corrections_applied;
    double estimated_clock_ppm;
    double clock_scale;
    double clock_offset_samples;
    double clock_fit_error_rms_samples;
    int clock_fit_points;
    double timing_error_before_rms;
    double timing_error_after_rms;

    double pilot_margin_sum;
    double pilot_offset_sq_sum;
    double timing_error_sq_sum;
    double span_sum;
    double clock_fit_sum_expected;
    double clock_fit_sum_observed;
    double clock_fit_sum_expected_sq;
    double clock_fit_sum_observed_sq;
    double clock_fit_sum_expected_observed;
    double timing_error_before_sq_sum;
    double timing_error_after_sq_sum;
    int timing_error_samples;
    int timing_error_compare_samples;
    int span_samples;
    int timing_search_symbols;
    int timing_search_offsets;

    TimingDiagnostics();
};

enum class DecodeFailureCause {
    None,
    Sync,
    HeaderFec,
    BodyFec,
    Crc,
    FalseLock,
    Incomplete,
    ConvergedButCrc
};

const char* decode_failure_cause_name(DecodeFailureCause cause);

struct DecodeAttemptDiagnostics {
    bool ok;
    DecodeFailureCause cause;
    fec::FecDecodeResult header_fec;
    fec::FecDecodeResult body_fec;
    bool sync_locked;
    double preamble_score;
    double sync_score;
    size_t detected_start_sample;
    double estimated_symbol_span;
    double selected_candidate_span;
    size_t number_of_symbols;
    size_t payload_bytes;
    size_t required_fec_bits;
    bool crc_ok;
    std::vector<uint8_t> header_decoded_bytes;
    demod::MetricStats metric_stats;
    TimingDiagnostics timing_diag;
    demod::DemodConfig demod_cfg_used;
    bool adaptive_channel_templates_enabled;
    int channel_template_known_symbols;
    double channel_template_energy;
    bool channel_template_fallback_used;
    double ideal_vs_adaptive_sync_score;
    bool weighted_correlation_enabled;
    double weight_min;
    double weight_max;
    double weight_mean;
    bool weight_fallback_used;

    DecodeAttemptDiagnostics();
};

struct ReceiverDiagnostics {
    DecodeAttemptDiagnostics decode;
    demod::DemodConfig demod_cfg;
    bool fec_enabled;
    const char* fec_mode;

    ReceiverDiagnostics();
};

}  // namespace receiver
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_RECEIVER_DIAGNOSTICS_H_
