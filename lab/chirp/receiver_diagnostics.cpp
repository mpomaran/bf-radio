#include "lab/chirp/receiver_diagnostics.h"

namespace chirp {
namespace receiver {

TimingDiagnostics::TimingDiagnostics()
    : pilot_count(0), pilot_used_for_timing(0),
      pilot_rejected_low_confidence(0), pilot_mean_margin(0.0),
      pilot_timing_offset_rms(0.0), timing_error_rms(0.0),
      span_estimate_mean(config::NOMINAL_SPAN),
      span_estimate_min(config::NOMINAL_SPAN),
      span_estimate_max(config::NOMINAL_SPAN), timing_search_full_count(0),
      timing_search_local_count(0), timing_search_center_count(0),
      average_offsets_per_symbol(0.0), timing_offset_initial_samples(0.0),
      timing_offset_final_samples(0.0), timing_corrections_applied(0),
      estimated_clock_ppm(0.0), clock_scale(1.0), clock_offset_samples(0.0),
      clock_fit_error_rms_samples(0.0), clock_fit_points(0),
      timing_error_before_rms(0.0), timing_error_after_rms(0.0),
      pilot_margin_sum(0.0), pilot_offset_sq_sum(0.0),
      timing_error_sq_sum(0.0), span_sum(0.0), clock_fit_sum_expected(0.0),
      clock_fit_sum_observed(0.0), clock_fit_sum_expected_sq(0.0),
      clock_fit_sum_observed_sq(0.0), clock_fit_sum_expected_observed(0.0),
      timing_error_before_sq_sum(0.0), timing_error_after_sq_sum(0.0),
      timing_error_samples(0), timing_error_compare_samples(0), span_samples(0),
      timing_search_symbols(0), timing_search_offsets(0) {}

const char* decode_failure_cause_name(DecodeFailureCause cause) {
    switch (cause) {
        case DecodeFailureCause::None: return "none";
        case DecodeFailureCause::Sync: return "sync";
        case DecodeFailureCause::HeaderFec: return "header_fec";
        case DecodeFailureCause::BodyFec: return "body_fec";
        case DecodeFailureCause::Crc: return "crc";
        case DecodeFailureCause::FalseLock: return "false_lock";
        case DecodeFailureCause::Incomplete: return "incomplete";
        case DecodeFailureCause::ConvergedButCrc: return "converged_but_crc";
    }
    return "unknown";
}

DecodeAttemptDiagnostics::DecodeAttemptDiagnostics()
    : ok(false), cause(DecodeFailureCause::Sync), header_fec(), body_fec(),
      sync_locked(false), preamble_score(0.0), sync_score(0.0),
      detected_start_sample(0), estimated_symbol_span(0.0),
      selected_candidate_span(0.0), number_of_symbols(0), payload_bytes(0),
      required_fec_bits(0), crc_ok(false), header_decoded_bytes(),
      metric_stats(), timing_diag(), demod_cfg_used(),
      adaptive_channel_templates_enabled(false),
      channel_template_known_symbols(0), channel_template_energy(0.0),
      channel_template_fallback_used(false), ideal_vs_adaptive_sync_score(0.0),
      weighted_correlation_enabled(false), weight_min(1.0), weight_max(1.0),
      weight_mean(1.0), weight_fallback_used(true) {}

ReceiverDiagnostics::ReceiverDiagnostics()
    : decode(), demod_cfg(demod::calibrated_llr_demod_config()),
      fec_enabled(true), fec_mode("ldpc-bp") {}

}  // namespace receiver
}  // namespace chirp
