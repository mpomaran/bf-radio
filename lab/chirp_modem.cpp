/*
  chirp_modem.cpp - experimental CSS/chirp modem prototype.

  Portable C++17 single-file modem for 8 kHz PCM16 audio. The waveform uses
  LoRa-like cyclic shifts of an up-chirp with 16 symbols. This version keeps the
  command line interface stable, but replaces the old structurally broken FEC
  with a small systematic sparse parity-check code and feeds it soft metrics
  from the chirp correlator.

  This is still a research prototype, not a production modem.
*/

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "lab/chirp/bit_utils.h"
#include "lab/chirp/adaptive_templates.h"
#include "lab/chirp/config.h"
#include "lab/chirp/demodulator.h"
#include "lab/chirp/demod_metrics.h"
#include "lab/chirp/fec_ldpc.h"
#include "lab/chirp/fft.h"
#include "lab/chirp/file_io.h"
#include "lab/chirp/frame.h"
#include "lab/chirp/interleaver.h"
#include "lab/chirp/modulator.h"
#include "lab/chirp/pcm_io.h"
#include "lab/chirp/receiver.h"
#include "lab/chirp/receiver_diagnostics.h"
#include "lab/chirp/receiver_options.h"
#include "lab/chirp/receiver_options_cli.h"
#include "lab/chirp/sample_view.h"
#include "lab/chirp/stream_decoder.h"
#include "lab/chirp/sync_acquisition.h"
#include "lab/chirp/timing_tracker.h"
#include "lab/chirp/waveform.h"
#include "lab/chirp/weighted_correlation.h"

using chirp::bits::binary_to_gray4;
using chirp::bits::bits_to_bytes;
using chirp::bits::bits_to_symbols;
using chirp::bits::bytes_to_bits;
using chirp::bits::descramble_llrs;
using chirp::bits::gray_to_binary4;
using chirp::adaptive::AdaptiveTemplateBank;
using chirp::adaptive::build_adaptive_template_bank;
using chirp::adaptive::ideal_base_precomputed_template;
using chirp::adaptive::update_adaptive_template_bank;
using chirp::config::ALPHABET;
using chirp::config::BITS_PER_SYMBOL;
using chirp::config::FEC_CODEWORD_BITS;
using chirp::config::FEC_INFO_BITS;
using chirp::config::NOMINAL_SPAN;
using chirp::config::PHY_VERSION;
using chirp::config::PILOT_INTERVAL_SYMBOLS;
using chirp::config::PILOT_SYMBOL;
using chirp::config::PREAMBLE_SYMBOLS;
using chirp::config::PROTOCOL_HEADER_BYTES;
using chirp::config::PROTOCOL_MAGIC;
using chirp::config::PROTOCOL_VERSION;
using chirp::config::SAMPLE_RATE;
using chirp::config::STREAM_DECODE_SYNC_SCORE_THRESHOLD;
using chirp::config::SYMBOL_SAMPLES;
using chirp::config::SYNC_SYMBOLS;
using chirp::config::PhyProfile;
using chirp::config::current_phy_profile;
using chirp::dsp::circular_chirp_correlation_diagnostics;
using chirp::dsp::FftCorrelationDiagnostics;
using chirp::dsp::reset_circular_chirp_correlation_diagnostics;
using chirp::demod::DemodConfig;
using chirp::demod::MetricStats;
using chirp::demod::SymbolMetrics;
using chirp::demod::calibrated_llr_demod_config;
using chirp::demod::fixed_llr_demod_config;
using chirp::demod::llr_mode_name;
using chirp::demod::metric_stats_observe_known_symbol;
using chirp::demod::symbol_metrics_to_llr;
using chirp::demodulator::decode_symbol_metrics_at;
using chirp::demodulator::estimate_metric_stats_from_known_symbols;
using chirp::demodulator::finalize_best_scores;
using chirp::demodulator::ideal_vs_adaptive_template_score_delta;
using chirp::fec::FecDecodeResult;
using chirp::fec::LDPCCodec;
using chirp::fec::fec_decode_bits_from_llr;
using chirp::fec::fec_decode_bits_from_llr_result;
using chirp::fec::fec_decode_bits_hard;
using chirp::fec::fec_encode_bits;
using chirp::frame::build_protected_frame;
using chirp::frame::decode_exact_payload_from_llrs;
using chirp::frame::fec_bits_for_info_bytes;
using chirp::frame::parse_protected_frame;
using chirp::frame::parse_protected_header;
using chirp::interleave::deinterleave;
using chirp::interleave::deinterleave_soft;
using chirp::interleave::interleave;
using chirp::interleave::interleave_soft;
using chirp::io::read_file;
using chirp::io::read_pcm16;
using chirp::io::write_file;
using chirp::io::write_pcm16;
using chirp::modulator::build_frame_tx_bits;
using chirp::modulator::encode_frame_bytes_to_pcm;
using chirp::modulator::encode_payload_to_pcm;
using chirp::modulator::pilot_count_for_data_symbols;
using chirp::receiver::decode_failure_cause_name;
using chirp::receiver::DecodeAttemptDiagnostics;
using chirp::receiver::DecodeFailureCause;
using chirp::receiver::parse_receiver_options_from_cli;
using chirp::receiver::ReceiverDiagnostics;
using chirp::receiver::ReceiverOptions;
using chirp::receiver::ReceiverProfile;
using chirp::receiver::receiver_demod_config;
using chirp::receiver::receiver_profile_name;
using chirp::receiver::TimingDiagnostics;
using chirp::receiver::TimingSearchProfile;
using chirp::receiver::timing_search_profile_name;
using chirp::sample::sample_at;
using chirp::stream::clamp_discard;
using chirp::stream::StreamScanResult;
using chirp::stream::StreamScanStatus;
using chirp::stream::stream_scan_status_name;
using chirp::sync::find_sync;
using chirp::sync::preamble_score_at;
using chirp::sync::SyncLock;
using chirp::timing::pilot_is_strong_for_timing;
using chirp::timing::TimingLoopConfig;
using chirp::timing::TimingState;
using chirp::timing::timing_diag_clock_tracking_is_safe;
using chirp::timing::timing_diag_has_clock_model;
using chirp::timing::timing_diag_predict_clock_sample;
using chirp::timing::timing_diag_record_clock_point;
using chirp::timing::timing_diag_record_clock_tracking_error;
using chirp::timing::timing_diag_record_error;
using chirp::timing::timing_diag_record_span;
using chirp::timing::timing_diag_record_sync_clock_points;
using chirp::timing::timing_loop_config_for_templates;
using chirp::waveform::append_symbol_pcm;
using chirp::weighted::build_weighted_correlation_model;
using chirp::weighted::WeightedCorrelationModel;

static void print_not_same_bitrate_notice() {
    const PhyProfile p = current_phy_profile();
    if (!p.same_bitrate_as_legacy) {
        std::cerr << "NOT SAME BITRATE: this mode trades bitrate/latency/overhead for robustness.\n";
    }
}

static void progress_message(bool enabled,
                             std::clock_t* last_report,
                             const std::string& message,
                             bool force = false) {
    if (!enabled || last_report == nullptr) return;
    const std::clock_t now = std::clock();
    const double elapsed = double(now - *last_report) / double(CLOCKS_PER_SEC);
    if (!force && elapsed < 0.75) return;
    *last_report = now;
    std::cerr << "[progress] " << message << "\n";
}

static ReceiverOptions g_receiver_options;

static DemodConfig receiver_demod_config() {
    return receiver_demod_config(g_receiver_options);
}

static void encode_file(const std::string& in_path, const std::string& out_pcm_path) {
    const std::vector<uint8_t> payload = read_file(in_path);
    const std::vector<int16_t> pcm = encode_payload_to_pcm(payload);
    write_pcm16(out_pcm_path, pcm);
    print_not_same_bitrate_notice();
    std::cerr << "Encoded " << payload.size() << " bytes into "
              << pcm.size() << " PCM samples, duration "
              << double(pcm.size()) / SAMPLE_RATE << " s\n";
}

static double percentile_from_samples(std::vector<double> samples, double percentile) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const double clamped = std::max(0.0, std::min(100.0, percentile));
    const size_t index = size_t(std::floor((clamped / 100.0) * double(samples.size() - 1)));
    return samples[index];
}

static DemodConfig calibrate_llr_from_known_symbols(const DemodConfig& base,
                                                    const MetricStats& stats) {
    DemodConfig cfg = base;
    cfg.known_symbol_count = stats.samples;
    cfg.known_symbol_margin_median =
        percentile_from_samples(stats.margin_samples, 50.0);
    cfg.known_symbol_margin_p05 =
        percentile_from_samples(stats.margin_samples, 5.0);

    if (!base.use_adaptive_llr || stats.samples < 4) {
        cfg.adaptive_llr_scale = 1.0;
        return cfg;
    }

    /*
      The existing llr_scale stays the base/manual multiplier. This frame-local
      factor gently raises confidence when known-symbol margins are healthy and
      lowers it when clipping, fading, multipath, or false locks make known
      symbols ambiguous.
    */
    const double robust_margin =
        std::max(0.0, 0.80 * cfg.known_symbol_margin_median +
                          0.20 * cfg.known_symbol_margin_p05);
    const double reference_margin = 8.0;
    const double raw_scale = robust_margin / reference_margin;
    cfg.adaptive_llr_scale = std::max(0.25, std::min(4.0, raw_scale));
    return cfg;
}

static double clamped_span_step(double step, double max_step) {
    return std::max(-max_step, std::min(max_step, step));
}

static std::vector<double> decode_llrs_tracking(const std::vector<int16_t>& pcm,
                                                double data_pos,
                                                double symbol_span,
                                                size_t max_bits = 50000 * BITS_PER_SYMBOL,
                                                AdaptiveTemplateBank* adaptive = nullptr,
                                                bool decision_directed_update = false,
                                                const DemodConfig& demod_cfg = DemodConfig(),
                                                MetricStats* metric_stats = nullptr,
                                                TimingDiagnostics* timing_diag = nullptr,
                                                const WeightedCorrelationModel* weights = nullptr,
                                                TimingSearchProfile timing_search_profile =
                                                    g_receiver_options.timing_search_profile,
                                                DemodConfig* effective_demod_cfg = nullptr,
                                                double clock_fit_base_expected_sample = 0.0) {
    std::vector<double> llrs;
    DemodConfig active_demod_cfg =
        calibrate_llr_from_known_symbols(demod_cfg,
                                         metric_stats != nullptr ? *metric_stats
                                                                 : MetricStats());
    if (effective_demod_cfg != nullptr) *effective_demod_cfg = active_demod_cfg;
    TimingState timing(data_pos, symbol_span);
    const double min_span = NOMINAL_SPAN * 0.85;
    const double max_span = NOMINAL_SPAN * 1.15;
    const bool adaptive_templates = adaptive != nullptr && adaptive->valid;
    const TimingLoopConfig loop_cfg = timing_loop_config_for_templates(adaptive_templates);
    const double template_update_confidence = 0.085;
    const double template_update_best_score = 0.30;
    const double template_update_max_timing_offset = 4.0;
    const double template_learning_rate = 0.010;
    size_t data_symbols = 0;
    size_t physical_symbols_since_data_start = 0;
    size_t next_pilot_after = size_t(PILOT_INTERVAL_SYMBOLS);
    bool timing_search_stable = false;
    timing_diag_record_span(timing_diag, timing.span);

    while (timing.pos + timing.span < double(pcm.size()) && llrs.size() < max_bits) {
        const double loop_predicted_pos = timing.pos;
        const double nominal_expected_sample =
            clock_fit_base_expected_sample +
            double(physical_symbols_since_data_start) * NOMINAL_SPAN;
        if (g_receiver_options.adaptive_clock_tracking_enabled &&
            timing_diag_clock_tracking_is_safe(timing_diag)) {
            timing.pos =
                timing_diag_predict_clock_sample(timing_diag, nominal_expected_sample);
            timing.span =
                std::max(min_span, std::min(max_span, timing_diag->clock_scale * NOMINAL_SPAN));
        }

        if (data_symbols == next_pilot_after) {
            const SymbolMetrics pilot =
                decode_symbol_metrics_at(pcm, timing.pos, timing.span, true,
                                         adaptive, weights, PILOT_SYMBOL,
                                         TimingSearchProfile::Full, timing_diag);
            double best_other = -1.0;
            for (int s = 0; s < ALPHABET; ++s) {
                if (s != PILOT_SYMBOL) {
                    best_other = std::max(best_other, pilot.metric[size_t(s)]);
                }
            }
            const double pilot_confidence =
                pilot.metric[size_t(PILOT_SYMBOL)] - best_other;
            const double observed_pilot_sample = timing.pos + pilot.timing_offset;
            const double clock_predicted_sample =
                timing_diag_has_clock_model(timing_diag)
                    ? timing_diag_predict_clock_sample(timing_diag, nominal_expected_sample)
                    : timing.pos;
            timing_diag_record_clock_tracking_error(
                timing_diag,
                observed_pilot_sample - loop_predicted_pos,
                observed_pilot_sample - clock_predicted_sample);
            timing_diag_record_clock_point(
                timing_diag,
                nominal_expected_sample,
                observed_pilot_sample);
            metric_stats_observe_known_symbol(metric_stats, pilot, PILOT_SYMBOL);
            active_demod_cfg =
                calibrate_llr_from_known_symbols(active_demod_cfg,
                                                 metric_stats != nullptr ? *metric_stats
                                                                         : MetricStats());
            if (effective_demod_cfg != nullptr) *effective_demod_cfg = active_demod_cfg;
            if (timing_diag != nullptr) {
                ++timing_diag->pilot_count;
                timing_diag->pilot_margin_sum += pilot_confidence;
                timing_diag->pilot_mean_margin =
                    timing_diag->pilot_margin_sum /
                    double(std::max(1, timing_diag->pilot_count));
                timing_diag->pilot_offset_sq_sum +=
                    pilot.timing_offset * pilot.timing_offset;
                timing_diag->pilot_timing_offset_rms =
                    std::sqrt(timing_diag->pilot_offset_sq_sum /
                              double(std::max(1, timing_diag->pilot_count)));
            }
            const bool strong_pilot =
                pilot_is_strong_for_timing(pilot_confidence, pilot.timing_offset,
                                           loop_cfg);
            if (adaptive != nullptr && adaptive->valid &&
                pilot.metric[size_t(PILOT_SYMBOL)] > 0.20 &&
                strong_pilot &&
                std::abs(pilot.timing_offset) <= template_update_max_timing_offset) {
                update_adaptive_template_bank(adaptive, pcm,
                                              timing.pos + pilot.timing_offset,
                                              timing.span, PILOT_SYMBOL,
                                              template_learning_rate * 1.8);
            }
            if (strong_pilot) {
                timing_search_stable = true;
                if (timing_diag != nullptr) ++timing_diag->pilot_used_for_timing;
                timing_diag_record_error(timing_diag, pilot.timing_offset);
                timing.timing_error_filtered =
                    0.80 * timing.timing_error_filtered + 0.20 * pilot.timing_offset;
                timing.span += clamped_span_step(loop_cfg.pilot_ki *
                                                 timing.timing_error_filtered,
                                                 loop_cfg.max_span_step);
                timing.span = std::max(min_span, std::min(max_span, timing.span));
                timing.pos += timing.span + loop_cfg.pilot_kp * pilot.timing_offset;
            } else {
                timing_search_stable = false;
                if (timing_diag != nullptr) ++timing_diag->pilot_rejected_low_confidence;
                timing.timing_error_filtered *= 0.98;
                timing.pos += timing.span;
            }
            timing_diag_record_span(timing_diag, timing.span);
            next_pilot_after += size_t(PILOT_INTERVAL_SYMBOLS);
            ++physical_symbols_since_data_start;
            continue;
        }

        TimingSearchProfile data_search = TimingSearchProfile::Full;
        if (timing_search_profile == TimingSearchProfile::CenterOnly) {
            data_search = TimingSearchProfile::CenterOnly;
        } else if (timing_search_profile == TimingSearchProfile::Local &&
                   timing_search_stable &&
                   std::abs(timing.timing_error_filtered) <= 2.0) {
            data_search = TimingSearchProfile::Local;
        }

        SymbolMetrics m =
            decode_symbol_metrics_at(pcm, timing.pos, timing.span, true, adaptive,
                                     weights, -1, data_search, timing_diag);
        double confidence = m.best_score - m.second_best_score;
        if (timing_search_profile == TimingSearchProfile::Local &&
            data_search == TimingSearchProfile::Local &&
            (confidence <= loop_cfg.confidence_threshold ||
             std::abs(m.timing_offset) > loop_cfg.max_timing_update)) {
            /*
              Local search is the normal fast path once timing is stable. If a
              symbol becomes ambiguous, re-run that same symbol with the full
              timing-offset set so low-confidence recovery keeps the old robust
              behavior.
            */
            m = decode_symbol_metrics_at(pcm, timing.pos, timing.span, true,
                                         adaptive, weights, -1,
                                         TimingSearchProfile::Full, timing_diag);
            confidence = m.best_score - m.second_best_score;
        }
        const std::array<double, BITS_PER_SYMBOL> symbol_llr =
            symbol_metrics_to_llr(m, active_demod_cfg, metric_stats);
        for (double v : symbol_llr) llrs.push_back(v);
        ++data_symbols;
        ++physical_symbols_since_data_start;

        if (decision_directed_update && adaptive != nullptr && adaptive->valid &&
            m.best_score > template_update_best_score &&
            confidence > template_update_confidence &&
            std::abs(m.timing_offset) <= template_update_max_timing_offset) {
            /*
              Decision-directed template tracking:
              use only high-confidence symbol decisions to slowly adapt the
              preamble-learned chirp shape. This follows slow speaker/recorder
              processing changes in long frames while avoiding runaway updates
              after weak or timing-ambiguous symbols.
            */
            update_adaptive_template_bank(adaptive, pcm, timing.pos + m.timing_offset,
                                          timing.span, m.best_symbol,
                                          template_learning_rate);
        }

        if (confidence > loop_cfg.confidence_threshold &&
            std::abs(m.timing_offset) <= loop_cfg.max_timing_update) {
            timing_search_stable = true;
            /*
              Sign convention:
              - m.timing_offset is the offset, in samples, that maximized the
                current symbol correlation.
              - Positive error means the best correlation is later than the
                predicted position, so the prediction was early.
              - pos advances by span plus a proportional correction.
              - span receives only a small integral correction from filtered
                timing error and is clamped to avoid runaway after a bad symbol.

              With preamble-adaptive templates, large apparent offsets are often
              caused by channel-shaped side lobes rather than real sample-clock
              drift. Those symbols are still demodulated, and may still update
              the template if their symbol decision is strong, but they do not
              pull the timing loop.
            */
            timing.timing_error_filtered =
                0.85 * timing.timing_error_filtered + 0.15 * m.timing_offset;
            timing_diag_record_error(timing_diag, m.timing_offset);
            timing.span += clamped_span_step(loop_cfg.data_ki *
                                             timing.timing_error_filtered,
                                             loop_cfg.max_span_step);
            timing.span = std::max(min_span, std::min(max_span, timing.span));
            timing.pos += timing.span + loop_cfg.data_kp * m.timing_offset;
        } else {
            timing_search_stable = false;
            timing.timing_error_filtered *= 0.98;
            timing.pos += timing.span;
        }
        timing_diag_record_span(timing_diag, timing.span);
    }
    return llrs;
}

static bool find_first_energy_sample(const std::vector<int16_t>& pcm, size_t* sample) {
    const size_t window = SYMBOL_SAMPLES / 2;
    if (pcm.size() <= window) return false;
    for (size_t i = 0; i + window < pcm.size(); i += 8) {
        double avg_abs = 0.0;
        for (size_t j = 0; j < window; ++j) avg_abs += std::abs(double(pcm[i + j]));
        avg_abs /= double(window);
        if (avg_abs > 1500.0) {
            *sample = i;
            return true;
        }
    }
    return false;
}

/*
  Streaming acquisition API.

  Call this repeatedly with the current rolling PCM buffer. On
  NoFrameWindowConsumed or InvalidFrameRejected, erase discard_prefix_samples
  from the front and keep scanning. On NeedMoreSamples, erase only
  discard_prefix_samples and append more PCM. On FrameDecoded, payload contains
  the protected frame payload and discard_prefix_samples consumes through the
  decoded frame.
*/
static StreamScanResult scan_pcm_window_for_frame(const std::vector<int16_t>& pcm,
                                                  bool verbose = false,
                                                  std::clock_t* progress_clock = nullptr,
                                                  const DemodConfig& demod_cfg =
                                                      calibrated_llr_demod_config()) {
    StreamScanResult result;
    result.status = StreamScanStatus::NoFrameWindowConsumed;

    const size_t max_sync_prefix = size_t(std::ceil((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                   NOMINAL_SPAN * 1.15 + 2.0 * NOMINAL_SPAN));
    const size_t min_sync_prefix = size_t(std::ceil((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                   NOMINAL_SPAN * 0.85));
    const size_t keep_tail_without_lock = max_sync_prefix;

    if (pcm.empty()) {
        progress_message(verbose, progress_clock, "scanner: empty PCM window", true);
        return result;
    }

    size_t energy = 0;
    if (!find_first_energy_sample(pcm, &energy)) {
        result.status = StreamScanStatus::NoFrameWindowConsumed;
        result.discard_prefix_samples = pcm.size();
        progress_message(verbose, progress_clock,
                         "scanner: no energy above threshold, consuming " +
                             std::to_string(pcm.size()) + " samples",
                         true);
        return result;
    }

    if (pcm.size() - energy < min_sync_prefix) {
        result.status = StreamScanStatus::NeedMoreSamples;
        result.discard_prefix_samples = energy;
        progress_message(verbose, progress_clock,
                         "scanner: possible signal near sample " + std::to_string(energy) +
                             ", need more samples for preamble/sync",
                         true);
        return result;
    }

    SyncLock lock;
    try {
        progress_message(verbose, progress_clock,
                         "scanner: searching sync in " + std::to_string(pcm.size()) +
                             " samples",
                         true);
        lock = find_sync(pcm, verbose, progress_clock);
    } catch (const std::exception&) {
        if (pcm.size() <= keep_tail_without_lock) {
            result.status = StreamScanStatus::NeedMoreSamples;
            result.discard_prefix_samples = std::min(energy, pcm.size());
            progress_message(verbose, progress_clock,
                             "scanner: no sync yet, keeping tail for more samples",
                             true);
            return result;
        }
        result.status = StreamScanStatus::NoFrameWindowConsumed;
        result.discard_prefix_samples = pcm.size() - keep_tail_without_lock;
        progress_message(verbose, progress_clock,
                         "scanner: sync not found, consuming " +
                             std::to_string(result.discard_prefix_samples) +
                             " inspected samples",
                         true);
        return result;
    }

    result.acquisition_score = lock.score;
    result.estimated_symbol_span = lock.symbol_span;
    const size_t frame_start = size_t(std::max(0.0, std::floor(lock.preamble_pos + 0.5)));
    result.frame_start_sample = frame_start;
    progress_message(verbose, progress_clock,
                     "scanner: sync candidate score " + std::to_string(lock.score) +
                         ", frame_start " + std::to_string(frame_start) +
                         ", span " + std::to_string(lock.symbol_span),
                     true);

    if (lock.score < STREAM_DECODE_SYNC_SCORE_THRESHOLD) {
        result.status = StreamScanStatus::InvalidFrameRejected;
        const size_t low_score_skip =
            std::max(frame_start + size_t(std::ceil(lock.symbol_span)), min_sync_prefix / 3);
        result.discard_prefix_samples = clamp_discard(low_score_skip, pcm.size());
        progress_message(verbose, progress_clock,
                         "scanner: sync score below decode threshold " +
                             std::to_string(STREAM_DECODE_SYNC_SCORE_THRESHOLD) +
                             ", skipping " +
                             std::to_string(result.discard_prefix_samples) +
                             " samples",
                         true);
        return result;
    }

    const AdaptiveTemplateBank adaptive =
        g_receiver_options.adaptive_channel_templates_enabled
            ? build_adaptive_template_bank(pcm, lock.preamble_pos, lock.symbol_span)
            : AdaptiveTemplateBank();
    progress_message(verbose, progress_clock,
                     adaptive.valid
                         ? "scanner: using preamble-adaptive channel templates"
                         : "scanner: using ideal channel templates",
                     true);

    const double data_pos = lock.sync_pos + SYNC_SYMBOLS * lock.symbol_span;
    bool saw_rejectable_candidate = false;
    const size_t header_symbols = FEC_CODEWORD_BITS / BITS_PER_SYMBOL;

    for (int tenths = 0; tenths <= 30; ++tenths) {
        const int signs[] = {1, -1};
        for (int sign : signs) {
            if (tenths == 0 && sign < 0) continue;
            const int signed_tenths = sign * tenths;
            const double candidate_span =
                lock.symbol_span * (1.0 + double(signed_tenths) / 1000.0);
            const int candidate_index = tenths * 2 + (sign < 0 ? 1 : 0);
            const int candidate_pct = int(100.0 * double(candidate_index) / 61.0);
            progress_message(verbose, progress_clock,
                             "scanner: checking frame candidate spans " +
                                 std::to_string(candidate_pct) + "%, drift " +
                                 std::to_string(double(signed_tenths) / 10.0) + "%");
            const double header_end = data_pos + double(header_symbols + 1) * candidate_span;
            if (header_end >= double(pcm.size())) {
                result.status = StreamScanStatus::NeedMoreSamples;
                result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                progress_message(verbose, progress_clock,
                                 "scanner: header candidate is incomplete, need more samples",
                                 true);
                return result;
            }

            const int template_modes =
                (g_receiver_options.adaptive_channel_templates_enabled && adaptive.valid) ? 2 : 1;
            for (int template_mode = 0; template_mode < template_modes; ++template_mode) {
                AdaptiveTemplateBank working_adaptive = adaptive;
                AdaptiveTemplateBank* decode_templates =
                    (template_mode == 0 && working_adaptive.valid) ? &working_adaptive : nullptr;
                const WeightedCorrelationModel weighted_model =
                    build_weighted_correlation_model(
                        pcm, lock.preamble_pos, lock.sync_pos, lock.symbol_span,
                        g_receiver_options.weighted_correlation_enabled,
                        decode_templates);
                const WeightedCorrelationModel* weights =
                    weighted_model.valid ? &weighted_model : nullptr;
                MetricStats metric_stats =
                    estimate_metric_stats_from_known_symbols(pcm, lock, decode_templates,
                                                             weights);
                if (template_mode == 1) {
                    progress_message(verbose, progress_clock,
                                     "scanner: retrying candidate with ideal templates",
                                     true);
                }

                const std::vector<double> header_llrs =
                    decode_llrs_tracking(pcm, data_pos, candidate_span,
                                         FEC_CODEWORD_BITS, decode_templates, false,
                                         demod_cfg, &metric_stats, nullptr, weights);
                if (header_llrs.size() < FEC_CODEWORD_BITS) {
                    result.status = StreamScanStatus::NeedMoreSamples;
                    result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                    progress_message(verbose, progress_clock,
                                     "scanner: header LLR extraction stopped at window end",
                                     true);
                    return result;
                }

                std::vector<double> header_tx(
                    header_llrs.begin(),
                    header_llrs.begin() + std::ptrdiff_t(FEC_CODEWORD_BITS));
                header_tx = descramble_llrs(header_tx);
                const FecDecodeResult header_fec =
                    fec_decode_bits_from_llr_result(header_tx);
                const std::vector<uint8_t> header_bytes = bits_to_bytes(header_fec.bits);

                size_t required_fec_bits = 0;
                if (!parse_protected_header(header_bytes, nullptr, &required_fec_bits)) {
                    saw_rejectable_candidate = true;
                    continue;
                }
                progress_message(verbose, progress_clock,
                                 "scanner: protected header decoded, frame FEC bits " +
                                     std::to_string(required_fec_bits),
                                 true);

                const size_t required_symbols =
                    (required_fec_bits + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL;
                const size_t required_physical_symbols =
                    required_symbols + pilot_count_for_data_symbols(required_symbols);
                const double frame_end =
                    data_pos + double(required_physical_symbols) * candidate_span;
                if (frame_end + candidate_span >= double(pcm.size())) {
                    result.status = StreamScanStatus::NeedMoreSamples;
                    result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                    progress_message(
                        verbose, progress_clock,
                        "scanner: frame appears valid but incomplete, need more samples",
                        true);
                    return result;
                }

                progress_message(verbose, progress_clock,
                                 "scanner: extracting full frame metrics, symbols " +
                                     std::to_string(required_symbols),
                                 true);
                const std::vector<double> frame_llrs =
                    decode_llrs_tracking(pcm, data_pos, candidate_span,
                                         required_fec_bits, decode_templates,
                                         decode_templates != nullptr,
                                         demod_cfg, &metric_stats, nullptr, weights);
                if (frame_llrs.size() < required_fec_bits) {
                    result.status = StreamScanStatus::NeedMoreSamples;
                    result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                    progress_message(verbose, progress_clock,
                                     "scanner: full-frame LLR extraction stopped at window end",
                                     true);
                    return result;
                }

                if (decode_exact_payload_from_llrs(frame_llrs, required_fec_bits,
                                                   &result.payload)) {
                    result.status = StreamScanStatus::FrameDecoded;
                    result.estimated_symbol_span = candidate_span;
                    result.frame_end_sample =
                        clamp_discard(size_t(std::ceil(frame_end)), pcm.size());
                    result.discard_prefix_samples = result.frame_end_sample;
                    progress_message(verbose, progress_clock,
                                     "scanner: frame decoded, consuming " +
                                         std::to_string(result.discard_prefix_samples) +
                                         " samples",
                                     true);
                    return result;
                }
                saw_rejectable_candidate = true;
            }
        }
    }

    result.status = saw_rejectable_candidate
                        ? StreamScanStatus::InvalidFrameRejected
                        : StreamScanStatus::NoFrameWindowConsumed;
    result.discard_prefix_samples =
        clamp_discard(frame_start + size_t(std::ceil(lock.symbol_span)), pcm.size());
    progress_message(verbose, progress_clock,
                     "scanner: candidate rejected, consuming " +
                         std::to_string(result.discard_prefix_samples) + " samples",
                     true);
    return result;
}

static DecodeAttemptDiagnostics diagnose_pcm_decode_attempt(
    const std::vector<int16_t>& pcm,
    std::vector<uint8_t>* decoded_payload,
    const DemodConfig& demod_cfg);

static bool decode_payload_from_pcm(const std::vector<int16_t>& pcm,
                                    std::vector<uint8_t>* payload,
                                    bool verbose = false,
                                    const DemodConfig& demod_cfg =
                                        calibrated_llr_demod_config()) {
    std::vector<int16_t> buffer = pcm;
    std::clock_t progress_clock = std::clock();
    progress_message(verbose, &progress_clock,
                     "decoder: input " + std::to_string(pcm.size()) + " samples (" +
                         std::to_string(double(pcm.size()) / SAMPLE_RATE) + " s)",
                     true);
    for (int iter = 0; iter < 128 && !buffer.empty(); ++iter) {
        progress_message(verbose, &progress_clock,
                         "decoder: scan iteration " + std::to_string(iter + 1) +
                             ", buffer " + std::to_string(buffer.size()) + " samples",
                         true);
        const StreamScanResult scan =
            scan_pcm_window_for_frame(buffer, verbose, &progress_clock, demod_cfg);
        progress_message(verbose, &progress_clock,
                         "decoder: scanner returned " +
                             std::string(stream_scan_status_name(scan.status)) +
                             ", discard " + std::to_string(scan.discard_prefix_samples) +
                             " samples",
                         true);
        if (scan.status == StreamScanStatus::FrameDecoded) {
            *payload = scan.payload;
            return true;
        }

        if (scan.discard_prefix_samples > 0) {
            const size_t discard = std::min(scan.discard_prefix_samples, buffer.size());
            buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            continue;
        }

        if (scan.status == StreamScanStatus::NeedMoreSamples) break;
        break;
    }
    return false;
}

namespace chirp {
namespace receiver {

DecodeResult decode_payload_from_pcm(const std::vector<int16_t>& pcm,
                                     const ReceiverOptions& options) {
    const ReceiverOptions saved_options = g_receiver_options;
    g_receiver_options = options;
    try {
        DecodeResult result;
        result.diagnostics =
            diagnose_pcm_decode_attempt(pcm, &result.payload, ::receiver_demod_config());
        result.ok = result.diagnostics.ok;
        g_receiver_options = saved_options;
        return result;
    } catch (...) {
        g_receiver_options = saved_options;
        throw;
    }
}

}  // namespace receiver
}  // namespace chirp

static void print_receiver_diagnostics_for_pcm(const std::vector<int16_t>& pcm,
                                               bool decode_ok,
                                               const std::vector<uint8_t>& payload);

static void decode_file(const std::string& in_pcm_path,
                        const std::string& out_path,
                        bool rx_diagnostics = false) {
    const std::vector<int16_t> pcm = read_pcm16(in_pcm_path);
    std::vector<uint8_t> payload;
    const DemodConfig demod_cfg = receiver_demod_config();
    const bool ok = decode_payload_from_pcm(pcm, &payload, true, demod_cfg);
    if (rx_diagnostics) {
        print_receiver_diagnostics_for_pcm(pcm, ok, payload);
    }
    if (!ok) {
        throw std::runtime_error("CRC check failed or valid frame not found");
    }
    write_file(out_path, payload);
    std::cerr << "Decoded " << payload.size() << " bytes OK\n";
}

static std::vector<int16_t> resample_pcm(const std::vector<int16_t>& pcm, double factor) {
    if (factor <= 0.0) throw std::runtime_error("Invalid resample factor");
    const size_t out_size = std::max<size_t>(1, size_t(std::floor(double(pcm.size()) * factor)));
    std::vector<int16_t> out;
    out.reserve(out_size);
    for (size_t i = 0; i < out_size; ++i) {
        double v = sample_at(pcm, double(i) / factor);
        v = std::max(-32768.0, std::min(32767.0, v));
        out.push_back(int16_t(std::round(v)));
    }
    return out;
}

static void append_silence(std::vector<int16_t>& pcm, int samples) {
    if (samples > 0) pcm.insert(pcm.end(), size_t(samples), 0);
}

static void append_white_noise(std::vector<int16_t>& pcm,
                               int samples,
                               double amplitude,
                               std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(-amplitude, amplitude);
    for (int i = 0; i < samples; ++i) {
        const double v = std::max(-32768.0, std::min(32767.0, dist(rng)));
        pcm.push_back(int16_t(std::round(v)));
    }
}

static void append_unrelated_chirp_burst(std::vector<int16_t>& pcm,
                                         int symbols,
                                         std::mt19937& rng) {
    std::uniform_int_distribution<int> sym_dist(0, ALPHABET - 1);
    for (int i = 0; i < symbols; ++i) append_symbol_pcm(pcm, sym_dist(rng));
}

static void append_fake_wrong_sync_transmission(std::vector<int16_t>& pcm,
                                                std::mt19937& rng) {
    for (int i = 0; i < PREAMBLE_SYMBOLS; ++i) append_symbol_pcm(pcm, 0);
    for (int i = 0; i < SYNC_SYMBOLS; ++i) append_symbol_pcm(pcm, (i * 5 + 6) & 0x0F);
    append_unrelated_chirp_burst(pcm, 20, rng);
}

static void append_fake_wrong_magic_transmission(std::vector<int16_t>& pcm,
                                                 std::mt19937& rng) {
    std::vector<uint8_t> fake_payload(24);
    for (uint8_t& b : fake_payload) b = uint8_t(rng() & 0xFFU);
    const uint8_t bad_magic[4] = {'N', 'O', 'P', 'E'};
    const std::vector<uint8_t> frame =
        build_protected_frame(fake_payload, bad_magic, PROTOCOL_VERSION, 0);
    const std::vector<int16_t> fake_pcm = encode_frame_bytes_to_pcm(frame);
    pcm.insert(pcm.end(), fake_pcm.begin(), fake_pcm.end());
}

static void append_corrupted_frame_like_burst(std::vector<int16_t>& pcm,
                                              std::mt19937& rng) {
    std::vector<uint8_t> payload(16);
    for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);
    const std::vector<int16_t> frame = encode_payload_to_pcm(payload);
    const size_t cut = std::min(frame.size(), size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS + 20) * SYMBOL_SAMPLES));
    pcm.insert(pcm.end(), frame.begin(), frame.begin() + std::ptrdiff_t(cut));
    append_white_noise(pcm, SYMBOL_SAMPLES * 4, 500.0, rng);
}

static void append_scaled(std::vector<int16_t>& dst,
                          const std::vector<int16_t>& src,
                          double gain) {
    for (int16_t s : src) {
        const double v = std::max(-32768.0, std::min(32767.0, double(s) * gain));
        dst.push_back(int16_t(std::round(v)));
    }
}

struct RawLinkMetrics {
    size_t symbols = 0;
    size_t symbol_errors = 0;
    size_t bits = 0;
    size_t bit_errors = 0;
    bool sync_locked = false;
};

static double active_signal_power(const std::vector<int16_t>& pcm) {
    double sum = 0.0;
    size_t count = 0;
    for (int16_t s : pcm) {
        if (std::abs(int(s)) < 8) continue;
        const double v = double(s);
        sum += v * v;
        ++count;
    }
    if (count == 0) return 1.0;
    return sum / double(count);
}

static std::vector<int16_t> add_awgn_for_snr(const std::vector<int16_t>& pcm,
                                             double snr_db,
                                             std::mt19937* rng) {
    const double power = active_signal_power(pcm);
    const double noise_power = power / std::pow(10.0, snr_db / 10.0);
    std::normal_distribution<double> noise(0.0, std::sqrt(noise_power));
    std::vector<int16_t> out;
    out.reserve(pcm.size());
    for (int16_t s : pcm) {
        double v = double(s) + noise(*rng);
        v = std::max(-32768.0, std::min(32767.0, v));
        out.push_back(int16_t(std::lround(v)));
    }
    return out;
}

static std::vector<int16_t> apply_quality_radio_channel(const std::vector<int16_t>& pcm,
                                                        double snr_db,
                                                        std::mt19937* rng) {
    std::vector<int16_t> work = resample_pcm(pcm, 1.002);
    std::vector<double> shaped(work.size() + 96, 0.0);
    const double pi = 3.14159265358979323846;
    double lp = 0.0;
    double prev_in = 0.0;
    double prev_hp = 0.0;

    for (size_t i = 0; i < work.size(); ++i) {
        const double fade = 1.0 - 0.10 * 0.5 *
                                      (1.0 - std::cos(2.0 * pi * double(i) / 36000.0));
        const double input = double(work[i]) * 0.78 * fade;
        const double hp = input - prev_in + 0.996 * prev_hp;
        prev_in = input;
        prev_hp = hp;
        lp += 0.84 * (hp - lp);
        shaped[i] += lp;
        shaped[i + 19] += 0.09 * lp;
        shaped[i + 67] += 0.025 * lp;
    }

    std::vector<int16_t> out;
    out.reserve(shaped.size());
    for (double v : shaped) {
        v = std::max(-30000.0, std::min(30000.0, v));
        out.push_back(int16_t(std::lround(v)));
    }
    out = add_awgn_for_snr(out, snr_db, rng);

    std::uniform_int_distribution<int> impulse_dist(-700, 700);
    for (size_t i = 2048; i < out.size(); i += 4096) {
        const int v = int(out[i]) + impulse_dist(*rng);
        out[i] = int16_t(std::max(-32768, std::min(32767, v)));
    }
    return out;
}

static RawLinkMetrics measure_raw_link_metrics_core(const std::vector<int16_t>& pcm,
                                                    const std::vector<uint8_t>& expected_tx_bits,
                                                    const std::vector<uint8_t>& expected_symbols,
                                                    bool oracle_select_best_candidate) {
    RawLinkMetrics metrics;
    metrics.symbols = expected_symbols.size();
    metrics.bits = expected_tx_bits.size();
    try {
        const SyncLock lock = find_sync(pcm, false, nullptr);
        metrics.sync_locked = true;
        const AdaptiveTemplateBank adaptive =
            build_adaptive_template_bank(pcm, lock.preamble_pos, lock.symbol_span);

        auto measure_candidate = [&](double candidate_span,
                                     const AdaptiveTemplateBank* adaptive_ptr) {
            RawLinkMetrics candidate;
            candidate.symbols = expected_symbols.size();
            candidate.bits = expected_tx_bits.size();
            candidate.sync_locked = true;
            TimingState timing(lock.sync_pos + SYNC_SYMBOLS * lock.symbol_span,
                               candidate_span);
            const double min_span = NOMINAL_SPAN * 0.85;
            const double max_span = NOMINAL_SPAN * 1.15;
            const double confidence_threshold = adaptive_ptr != nullptr ? 0.055 : 0.035;
            const double timing_update_max_offset = adaptive_ptr != nullptr ? 4.0 : 24.0;
            const double kp = adaptive_ptr != nullptr ? 0.18 : 0.55;
            const double ki = adaptive_ptr != nullptr ? 0.003 : 0.015;

            for (size_t i = 0; i < expected_symbols.size(); ++i) {
                if (i > 0 && (i % size_t(PILOT_INTERVAL_SYMBOLS)) == 0) {
                    const SymbolMetrics pilot =
                        decode_symbol_metrics_at(pcm, timing.pos, timing.span, true,
                                                 adaptive_ptr, nullptr, PILOT_SYMBOL);
                    if (std::abs(pilot.timing_offset) <= timing_update_max_offset) {
                        timing.timing_error_filtered =
                            0.80 * timing.timing_error_filtered + 0.20 * pilot.timing_offset;
                        timing.span += 1.5 * ki * timing.timing_error_filtered;
                        timing.span = std::max(min_span, std::min(max_span, timing.span));
                        timing.pos += timing.span + 0.75 * kp * pilot.timing_offset;
                    } else {
                        timing.pos += timing.span;
                    }
                }
                if (timing.pos + timing.span >= double(pcm.size())) {
                    candidate.symbol_errors += expected_symbols.size() - i;
                    candidate.bit_errors += expected_tx_bits.size() - i * BITS_PER_SYMBOL;
                    break;
                }

                const SymbolMetrics m =
                    decode_symbol_metrics_at(pcm, timing.pos, timing.span, true,
                                             adaptive_ptr);
                if (uint8_t(m.best_symbol) != expected_symbols[i]) ++candidate.symbol_errors;

                const uint8_t binary_symbol = gray_to_binary4(uint8_t(m.best_symbol));
                for (int bit = 0; bit < BITS_PER_SYMBOL; ++bit) {
                    const size_t bit_index = i * BITS_PER_SYMBOL + size_t(bit);
                    if (bit_index >= expected_tx_bits.size()) break;
                    const uint8_t got =
                        uint8_t((binary_symbol >> (BITS_PER_SYMBOL - 1 - bit)) & 1);
                    if (got != (expected_tx_bits[bit_index] & 1)) ++candidate.bit_errors;
                }

                const double confidence = m.best_score - m.second_best_score;
                if (confidence > confidence_threshold &&
                    std::abs(m.timing_offset) <= timing_update_max_offset) {
                    timing.timing_error_filtered =
                        0.85 * timing.timing_error_filtered + 0.15 * m.timing_offset;
                    timing.span += ki * timing.timing_error_filtered;
                    timing.span = std::max(min_span, std::min(max_span, timing.span));
                    timing.pos += timing.span + kp * m.timing_offset;
                } else {
                    timing.timing_error_filtered *= 0.98;
                    timing.pos += timing.span;
                }
            }
            return candidate;
        };

        metrics.symbol_errors = metrics.symbols;
        metrics.bit_errors = metrics.bits;
        const int span_tenths[] = {0, 1, -1, 2, -2, 5, -5, 10, -10, 20, -20, 30, -30};
        for (int signed_tenths : span_tenths) {
            const double candidate_span =
                lock.symbol_span * (1.0 + double(signed_tenths) / 1000.0);
            const int template_modes = adaptive.valid ? 2 : 1;
            for (int template_mode = 0; template_mode < template_modes; ++template_mode) {
                const AdaptiveTemplateBank* adaptive_ptr =
                    (template_mode == 0 && adaptive.valid) ? &adaptive : nullptr;
                const RawLinkMetrics candidate =
                    measure_candidate(candidate_span, adaptive_ptr);
                /*
                  Receiver-selected raw metrics must not use TX truth to pick a
                  candidate. In this branch the candidate is chosen solely from
                  the receiver's deterministic scan order; expected bits are
                  compared only after selection to count SER/BER.
                */
                if (!oracle_select_best_candidate) return candidate;
                if (candidate.bit_errors < metrics.bit_errors) metrics = candidate;
            }
        }
    } catch (const std::exception&) {
        metrics.sync_locked = false;
        metrics.symbol_errors = metrics.symbols;
        metrics.bit_errors = metrics.bits / 2;
    }
    return metrics;
}

static RawLinkMetrics measure_receiver_selected_raw_link_metrics(
    const std::vector<int16_t>& pcm,
    const std::vector<uint8_t>& expected_tx_bits,
    const std::vector<uint8_t>& expected_symbols) {
    return measure_raw_link_metrics_core(pcm, expected_tx_bits, expected_symbols, false);
}

static RawLinkMetrics measure_oracle_raw_link_metrics(
    const std::vector<int16_t>& pcm,
    const std::vector<uint8_t>& expected_tx_bits,
    const std::vector<uint8_t>& expected_symbols) {
    return measure_raw_link_metrics_core(pcm, expected_tx_bits, expected_symbols, true);
}

static std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(hex[(b >> 4) & 0x0F]);
        out.push_back(hex[b & 0x0F]);
    }
    return out;
}

static double finite_or_zero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

static void print_receiver_diagnostics_json(const ReceiverDiagnostics& rx) {
    const DecodeAttemptDiagnostics& d = rx.decode;
    const MetricStats& m = d.metric_stats;
    const TimingDiagnostics& t = d.timing_diag;
    const PhyProfile phy = current_phy_profile();
    const int ldpc_iterations =
        std::max(d.header_fec.max_iterations, d.body_fec.max_iterations);
    const bool ldpc_success =
        d.header_fec.all_blocks_ok &&
        (d.body_fec.block_count == 0 || d.body_fec.all_blocks_ok);
    const int fec_failed_blocks = d.header_fec.failed_blocks + d.body_fec.failed_blocks;
    const int fec_syndrome =
        d.header_fec.total_syndrome_weight + d.body_fec.total_syndrome_weight;
    const double margin_min =
        std::isfinite(m.margin_min) ? m.margin_min : 0.0;
    const DemodConfig& used_cfg =
        d.demod_cfg_used.known_symbol_count > 0 ? d.demod_cfg_used : rx.demod_cfg;
    const double llr_scale_used =
        used_cfg.llr_scale *
        (used_cfg.use_adaptive_llr ? used_cfg.adaptive_llr_scale : 1.0);

    std::cerr
        << "rx_diagnostics={"
        << "\"preamble_score\":" << finite_or_zero(d.preamble_score) << ","
        << "\"sync_score\":" << finite_or_zero(d.sync_score) << ","
        << "\"detected_start_sample\":" << d.detected_start_sample << ","
        << "\"number_of_symbols\":" << d.number_of_symbols << ","
        << "\"payload_bytes\":" << d.payload_bytes << ","
        << "\"fec_enabled\":" << (rx.fec_enabled ? "true" : "false") << ","
        << "\"fec_mode\":\"" << rx.fec_mode << "\","
        << "\"rx_profile\":\"" << receiver_profile_name(g_receiver_options.receiver_profile) << "\","
        << "\"robust_defaults_enabled\":"
        << (g_receiver_options.receiver_profile == ReceiverProfile::Robust ? "true" : "false") << ","
        << "\"legacy_baseline_available\":true,"
        << "\"same_bitrate_as_legacy\":"
        << (phy.same_bitrate_as_legacy ? "true" : "false") << ","
        << "\"same_channel_as_legacy\":"
        << (phy.same_channel_as_legacy ? "true" : "false") << ","
        << "\"estimated_clock_ppm\":"
        << finite_or_zero(t.estimated_clock_ppm) << ","
        << "\"adaptive_clock_tracking_enabled\":"
        << (g_receiver_options.adaptive_clock_tracking_enabled ? "true" : "false") << ","
        << "\"adaptive_channel_templates_enabled\":"
        << (d.adaptive_channel_templates_enabled ? "true" : "false") << ","
        << "\"channel_template_known_symbols\":"
        << d.channel_template_known_symbols << ","
        << "\"channel_template_energy\":"
        << finite_or_zero(d.channel_template_energy) << ","
        << "\"channel_template_fallback_used\":"
        << (d.channel_template_fallback_used ? "true" : "false") << ","
        << "\"ideal_vs_adaptive_sync_score\":"
        << finite_or_zero(d.ideal_vs_adaptive_sync_score) << ","
        << "\"weighted_correlation_enabled\":"
        << (d.weighted_correlation_enabled ? "true" : "false") << ","
        << "\"weight_min\":" << finite_or_zero(d.weight_min) << ","
        << "\"weight_max\":" << finite_or_zero(d.weight_max) << ","
        << "\"weight_mean\":" << finite_or_zero(d.weight_mean) << ","
        << "\"weight_fallback_used\":"
        << (d.weight_fallback_used ? "true" : "false") << ","
        << "\"clock_scale\":" << finite_or_zero(t.clock_scale) << ","
        << "\"clock_fit_error_rms_samples\":"
        << finite_or_zero(t.clock_fit_error_rms_samples) << ","
        << "\"clock_fit_points\":" << t.clock_fit_points << ","
        << "\"timing_error_before_rms\":"
        << finite_or_zero(t.timing_error_before_rms) << ","
        << "\"timing_error_after_rms\":"
        << finite_or_zero(t.timing_error_after_rms) << ","
        << "\"timing_offset_initial_samples\":"
        << finite_or_zero(t.timing_offset_initial_samples) << ","
        << "\"timing_offset_final_samples\":"
        << finite_or_zero(t.timing_offset_final_samples) << ","
        << "\"timing_corrections_applied\":" << t.timing_corrections_applied << ","
        << "\"pilot_count\":" << t.pilot_count << ","
        << "\"pilot_interval_symbols\":" << PILOT_INTERVAL_SYMBOLS << ","
        << "\"winner_score_mean\":" << finite_or_zero(m.winner_mean) << ","
        << "\"runner_up_score_mean\":" << finite_or_zero(m.runner_up_mean) << ","
        << "\"margin_mean\":" << finite_or_zero(m.mean_peak_margin) << ","
        << "\"margin_p05\":"
        << finite_or_zero(percentile_from_samples(m.margin_samples, 5.0)) << ","
        << "\"margin_min\":" << finite_or_zero(margin_min) << ","
        << "\"symbol_error_estimate_on_known_symbols\":"
        << finite_or_zero(m.symbol_error_rate) << ","
        << "\"llr_mean_abs\":" << finite_or_zero(m.llr_mean_abs) << ","
        << "\"llr_max_abs\":" << finite_or_zero(m.llr_max_abs) << ","
        << "\"llr_saturation_count\":" << m.llr_saturated << ","
        << "\"llr_saturation_rate\":" << finite_or_zero(m.llr_saturation_rate) << ","
        << "\"llr_clip_value\":" << used_cfg.llr_clip << ","
        << "\"llr_scale_used\":" << finite_or_zero(llr_scale_used) << ","
        << "\"adaptive_llr_enabled\":"
        << (used_cfg.use_adaptive_llr ? "true" : "false") << ","
        << "\"adaptive_llr_scale\":" << finite_or_zero(used_cfg.adaptive_llr_scale) << ","
        << "\"known_symbol_margin_median\":"
        << finite_or_zero(used_cfg.known_symbol_margin_median) << ","
        << "\"known_symbol_margin_p05\":"
        << finite_or_zero(used_cfg.known_symbol_margin_p05) << ","
        << "\"known_symbol_count\":" << used_cfg.known_symbol_count << ","
        << "\"ldpc_iterations_used\":" << ldpc_iterations << ","
        << "\"ldpc_decode_success\":" << (ldpc_success ? "true" : "false") << ","
        << "\"fec_failed_blocks\":" << fec_failed_blocks << ","
        << "\"fec_syndrome_weight\":" << fec_syndrome << ","
        << "\"crc_ok\":" << (d.crc_ok ? "true" : "false") << ","
        << "\"decode_success\":" << (d.ok ? "true" : "false") << ","
        << "\"failure_cause\":\"" << decode_failure_cause_name(d.cause) << "\""
        << "}\n";
}

static void print_receiver_diagnostics_for_pcm(const std::vector<int16_t>& pcm,
                                               bool decode_ok,
                                               const std::vector<uint8_t>& payload) {
    ReceiverDiagnostics rx_diag;
    std::vector<uint8_t> diagnostic_payload;
    rx_diag.demod_cfg = receiver_demod_config();
    rx_diag.decode = diagnose_pcm_decode_attempt(pcm, &diagnostic_payload,
                                                 rx_diag.demod_cfg);
    if (decode_ok && rx_diag.decode.ok && diagnostic_payload.size() != payload.size()) {
        rx_diag.decode.payload_bytes = payload.size();
    }
    print_receiver_diagnostics_json(rx_diag);
}

static DecodeAttemptDiagnostics diagnose_pcm_decode_attempt(
    const std::vector<int16_t>& pcm,
    std::vector<uint8_t>* decoded_payload,
    const DemodConfig& demod_cfg = calibrated_llr_demod_config()) {
    DecodeAttemptDiagnostics diag;
    try {
        const SyncLock lock = find_sync(pcm, false, nullptr);
        diag.sync_locked = true;
        diag.preamble_score = preamble_score_at(pcm, lock.preamble_pos, lock.symbol_span);
        diag.sync_score = lock.score;
        diag.detected_start_sample =
            size_t(std::max(0.0, std::floor(lock.preamble_pos + 0.5)));
        diag.estimated_symbol_span = lock.symbol_span;
        diag.adaptive_channel_templates_enabled =
            g_receiver_options.adaptive_channel_templates_enabled;
        diag.weighted_correlation_enabled = g_receiver_options.weighted_correlation_enabled;
        if (lock.score < STREAM_DECODE_SYNC_SCORE_THRESHOLD) {
            diag.cause = DecodeFailureCause::FalseLock;
            return diag;
        }

        const AdaptiveTemplateBank adaptive =
            g_receiver_options.adaptive_channel_templates_enabled
                ? build_adaptive_template_bank(pcm, lock.preamble_pos, lock.symbol_span)
                : AdaptiveTemplateBank();
        diag.channel_template_known_symbols = adaptive.known_symbols;
        diag.channel_template_energy = adaptive.template_energy;
        diag.channel_template_fallback_used =
            !g_receiver_options.adaptive_channel_templates_enabled || !adaptive.valid;
        diag.ideal_vs_adaptive_sync_score =
            ideal_vs_adaptive_template_score_delta(pcm, lock, adaptive);
        const double data_pos = lock.sync_pos + SYNC_SYMBOLS * lock.symbol_span;
        const size_t header_symbols = FEC_CODEWORD_BITS / BITS_PER_SYMBOL;
        bool saw_incomplete = false;
        bool saw_header_candidate = false;
        bool saw_valid_header = false;

        for (int tenths = 0; tenths <= 30; ++tenths) {
            const int signs[] = {1, -1};
            for (int sign : signs) {
                if (tenths == 0 && sign < 0) continue;
                const int signed_tenths = sign * tenths;
                const double candidate_span =
                    lock.symbol_span * (1.0 + double(signed_tenths) / 1000.0);
                const double header_end =
                    data_pos + double(header_symbols + 1) * candidate_span;
                if (header_end >= double(pcm.size())) {
                    saw_incomplete = true;
                    continue;
                }

                const int template_modes =
                    (g_receiver_options.adaptive_channel_templates_enabled && adaptive.valid) ? 2 : 1;
                for (int template_mode = 0; template_mode < template_modes; ++template_mode) {
                    AdaptiveTemplateBank working_adaptive = adaptive;
                    AdaptiveTemplateBank* adaptive_ptr =
                        (template_mode == 0 && working_adaptive.valid) ? &working_adaptive : nullptr;
                    const WeightedCorrelationModel weighted_model =
                        build_weighted_correlation_model(
                            pcm, lock.preamble_pos, lock.sync_pos, lock.symbol_span,
                            g_receiver_options.weighted_correlation_enabled,
                            adaptive_ptr);
                    const WeightedCorrelationModel* weights =
                        weighted_model.valid ? &weighted_model : nullptr;
                    MetricStats metric_stats =
                        estimate_metric_stats_from_known_symbols(pcm, lock, adaptive_ptr,
                                                                 weights);
                    TimingDiagnostics timing_diag;
                    timing_diag_record_sync_clock_points(&timing_diag, lock);
                    DemodConfig header_demod_cfg;
                    std::vector<double> header_llrs =
                        decode_llrs_tracking(pcm, data_pos, candidate_span,
                                             FEC_CODEWORD_BITS, adaptive_ptr, false,
                                             demod_cfg, &metric_stats, &timing_diag,
                                             weights,
                                             g_receiver_options.timing_search_profile, &header_demod_cfg,
                                             double(PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                 NOMINAL_SPAN);
                    if (header_llrs.size() < FEC_CODEWORD_BITS) {
                        saw_incomplete = true;
                        continue;
                    }
                    header_llrs = descramble_llrs(header_llrs);
                    const FecDecodeResult header_fec =
                        fec_decode_bits_from_llr_result(header_llrs);
                    std::vector<uint8_t> header_bytes = bits_to_bytes(header_fec.bits);
                    header_bytes.resize(PROTOCOL_HEADER_BYTES);
                    if (!saw_header_candidate) {
                        diag.header_fec = header_fec;
                        diag.header_decoded_bytes = header_bytes;
                        diag.selected_candidate_span = candidate_span;
                        diag.metric_stats = metric_stats;
                        diag.timing_diag = timing_diag;
                        diag.demod_cfg_used = header_demod_cfg;
                        diag.channel_template_fallback_used = adaptive_ptr == nullptr;
                        diag.weight_min = weighted_model.weight_min;
                        diag.weight_max = weighted_model.weight_max;
                        diag.weight_mean = weighted_model.weight_mean;
                        diag.weight_fallback_used =
                            g_receiver_options.weighted_correlation_enabled && !weighted_model.valid;
                        saw_header_candidate = true;
                    }

                    size_t required_fec_bits = 0;
                    if (!parse_protected_header(header_bytes, nullptr, &required_fec_bits)) {
                        continue;
                    }
                    saw_valid_header = true;
                    diag.header_fec = header_fec;
                    diag.header_decoded_bytes = header_bytes;
                    diag.selected_candidate_span = candidate_span;
                    diag.required_fec_bits = required_fec_bits;
                    diag.metric_stats = metric_stats;
                    diag.timing_diag = timing_diag;
                    diag.demod_cfg_used = header_demod_cfg;
                    diag.channel_template_fallback_used = adaptive_ptr == nullptr;
                    diag.weight_min = weighted_model.weight_min;
                    diag.weight_max = weighted_model.weight_max;
                    diag.weight_mean = weighted_model.weight_mean;
                    diag.weight_fallback_used =
                        g_receiver_options.weighted_correlation_enabled && !weighted_model.valid;

                    const size_t required_symbols =
                        (required_fec_bits + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL;
                    diag.number_of_symbols = required_symbols;
                    const size_t required_physical_symbols =
                        required_symbols + pilot_count_for_data_symbols(required_symbols);
                    const double frame_end =
                        data_pos + double(required_physical_symbols) * candidate_span;
                    if (frame_end + candidate_span >= double(pcm.size())) {
                        saw_incomplete = true;
                        continue;
                    }

                    AdaptiveTemplateBank full_adaptive = adaptive;
                    AdaptiveTemplateBank* full_adaptive_ptr =
                        (template_mode == 0 && full_adaptive.valid) ? &full_adaptive : nullptr;
                    DemodConfig full_demod_cfg;
                    const std::vector<double> frame_llrs =
                        decode_llrs_tracking(pcm, data_pos, candidate_span,
                                             required_fec_bits, full_adaptive_ptr,
                                             full_adaptive_ptr != nullptr,
                                             demod_cfg, &metric_stats, &timing_diag,
                                             weights,
                                             g_receiver_options.timing_search_profile, &full_demod_cfg,
                                             double(PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                 NOMINAL_SPAN);
                    if (frame_llrs.size() < required_fec_bits) {
                        saw_incomplete = true;
                        continue;
                    }

                    std::vector<uint8_t> payload;
                    const bool ok = decode_exact_payload_from_llrs(frame_llrs, required_fec_bits,
                                                                   &payload,
                                                                   &diag.header_fec,
                                                                   &diag.body_fec);
                    diag.metric_stats = metric_stats;
                    diag.timing_diag = timing_diag;
                    diag.demod_cfg_used = full_demod_cfg;
                    diag.channel_template_fallback_used = full_adaptive_ptr == nullptr;
                    diag.weight_min = weighted_model.weight_min;
                    diag.weight_max = weighted_model.weight_max;
                    diag.weight_mean = weighted_model.weight_mean;
                    diag.weight_fallback_used =
                        g_receiver_options.weighted_correlation_enabled && !weighted_model.valid;
                    if (ok) {
                        diag.ok = true;
                        diag.cause = DecodeFailureCause::None;
                        diag.crc_ok = true;
                        diag.payload_bytes = payload.size();
                        if (decoded_payload) *decoded_payload = payload;
                        return diag;
                    }
                    diag.crc_ok = false;
                    if (!diag.body_fec.all_blocks_ok) diag.cause = DecodeFailureCause::BodyFec;
                    else diag.cause = DecodeFailureCause::ConvergedButCrc;
                }
            }
        }
        if (!saw_valid_header) {
            diag.cause = saw_incomplete ? DecodeFailureCause::Incomplete
                                        : DecodeFailureCause::HeaderFec;
        }
        return diag;
    } catch (const std::exception&) {
        diag.cause = DecodeFailureCause::Sync;
        return diag;
    }
}

static size_t count_payload_bit_errors(const std::vector<uint8_t>& expected,
                                       const std::vector<uint8_t>& got) {
    const size_t max_size = std::max(expected.size(), got.size());
    size_t errors = 0;
    for (size_t i = 0; i < max_size; ++i) {
        const uint8_t a = i < expected.size() ? expected[i] : 0;
        const uint8_t b = i < got.size() ? got[i] : 0;
        uint8_t diff = uint8_t(a ^ b);
        for (int bit = 0; bit < 8; ++bit) {
            errors += (diff >> bit) & 1U;
        }
    }
    return errors;
}

struct StatisticalLinkResult {
    size_t symbols = 0;
    size_t symbol_errors = 0;
    size_t bits = 0;
    size_t bit_errors = 0;
    size_t packets = 0;
    size_t packet_errors = 0;
};

static SymbolMetrics simulate_metric_channel_symbol(uint8_t raw_symbol,
                                                    double snr_db,
                                                    bool radio_profile,
                                                    size_t symbol_index,
                                                    std::mt19937* rng) {
    /*
      Fast statistical measurement model:
      - AWGN profile models a 16-way CSS correlator metric vector directly.
      - Radio profile adds residual implementation damage: lower effective
        metric SNR, neighbor-symbol leakage, and short burst erasures. This is
        deliberately a measurement surrogate, not the full PCM receiver.
    */
    const double effective_snr_db = radio_profile ? snr_db - 4.0 : snr_db;
    const double snr_linear = std::pow(10.0, effective_snr_db / 10.0);
    const double signal = std::sqrt(2.0 * std::max(0.0, snr_linear));
    const double sigma = 0.55;
    std::normal_distribution<double> gaussian(0.0, sigma);

    SymbolMetrics m;
    for (int s = 0; s < ALPHABET; ++s) {
        m.metric[size_t(s)] = gaussian(*rng);
    }

    m.metric[size_t(raw_symbol)] += signal;
    if (radio_profile) {
        const int prev = (int(raw_symbol) + ALPHABET - 1) & 0x0F;
        const int next = (int(raw_symbol) + 1) & 0x0F;
        m.metric[size_t(prev)] += 0.22 * signal;
        m.metric[size_t(next)] += 0.30 * signal;
        if ((symbol_index % 97) >= 90) {
            for (int s = 0; s < ALPHABET; ++s) {
                m.metric[size_t(s)] += gaussian(*rng) * 1.4;
            }
            m.metric[size_t(raw_symbol)] -= 0.45 * signal;
        }
    }

    m.timing_offset = 0.0;
    finalize_best_scores(&m);
    return m;
}

static StatisticalLinkResult simulate_statistical_link(double snr_db,
                                                       bool radio_profile,
                                                       int trials,
                                                       std::mt19937* rng) {
    StatisticalLinkResult result;
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<uint8_t> payload(64);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = uint8_t((trial * 131 + int(i) * 17 + int((*rng)())) & 0xFF);
        }

        const std::vector<uint8_t> frame =
            build_protected_frame(payload, PROTOCOL_MAGIC, PROTOCOL_VERSION, 0);
        const std::vector<uint8_t> tx_bits = build_frame_tx_bits(frame);
        const std::vector<uint8_t> tx_symbols = bits_to_symbols(tx_bits);
        std::vector<double> llrs;
        llrs.reserve(tx_bits.size());

        for (size_t i = 0; i < tx_symbols.size(); ++i) {
            const SymbolMetrics m =
                simulate_metric_channel_symbol(tx_symbols[i], snr_db, radio_profile, i, rng);
            if (uint8_t(m.best_symbol) != tx_symbols[i]) ++result.symbol_errors;
            ++result.symbols;

            const uint8_t hard_binary = gray_to_binary4(uint8_t(m.best_symbol));
            const std::array<double, BITS_PER_SYMBOL> symbol_llr = symbol_metrics_to_llr(m);
            for (int bit = 0; bit < BITS_PER_SYMBOL; ++bit) {
                const size_t bit_index = i * BITS_PER_SYMBOL + size_t(bit);
                if (bit_index >= tx_bits.size()) break;
                const uint8_t hard_bit =
                    uint8_t((hard_binary >> (BITS_PER_SYMBOL - 1 - bit)) & 1);
                if (hard_bit != (tx_bits[bit_index] & 1)) ++result.bit_errors;
                ++result.bits;
                llrs.push_back(symbol_llr[size_t(bit)]);
            }
        }

        std::vector<uint8_t> decoded;
        const bool ok = decode_exact_payload_from_llrs(llrs, tx_bits.size(), &decoded);
        if (!ok || decoded != payload) ++result.packet_errors;
        ++result.packets;
    }
    return result;
}

static void run_statistical_quality_measurement(int trials_per_point = 500) {
    if (trials_per_point <= 0) throw std::runtime_error("measure trials must be positive");
    const double snr_points[] = {12.0, 9.0, 6.0, 3.0, 0.0, -3.0, -6.0};
    std::mt19937 rng(0x5EED500u);

    std::cout << "profile,snr_db,trials,raw_ser,raw_ber,per\n";
    for (const char* profile : {"awgn-metric", "radio-metric"}) {
        const bool radio = std::string(profile) == "radio-metric";
        for (double snr_db : snr_points) {
            const StatisticalLinkResult r =
                simulate_statistical_link(snr_db, radio, trials_per_point, &rng);
            const double raw_ser =
                r.symbols ? double(r.symbol_errors) / double(r.symbols) : 1.0;
            const double raw_ber = r.bits ? double(r.bit_errors) / double(r.bits) : 0.5;
            const double per = r.packets ? double(r.packet_errors) / double(r.packets) : 1.0;
            std::cout << profile << ","
                      << snr_db << ","
                      << trials_per_point << ","
                      << raw_ser << ","
                      << raw_ber << ","
                      << per << "\n";
        }
    }
}

static void run_pcm_quality_measurement(int trials_per_point = 2,
                                        const DemodConfig& demod_cfg =
                                            calibrated_llr_demod_config(),
                                        const char* profile_filter = nullptr,
                                        const double* explicit_snr_points = nullptr,
                                        size_t explicit_snr_point_count = 0,
                                        bool print_header = true,
                                        bool include_oracle_metrics = true) {
    if (trials_per_point <= 0) throw std::runtime_error("measure trials must be positive");
    const double smoke_snr_points[] = {24.0};
    const double full_snr_points[] = {24.0, 18.0, 12.0, 6.0, 3.0, 0.0, -3.0, -6.0};
    const double* snr_points =
        explicit_snr_points != nullptr ? explicit_snr_points
                                       : (trials_per_point == 1 ? smoke_snr_points
                                                                : full_snr_points);
    const size_t snr_point_count =
        explicit_snr_points != nullptr
            ? explicit_snr_point_count
            : (trials_per_point == 1
                   ? sizeof(smoke_snr_points) / sizeof(smoke_snr_points[0])
                   : sizeof(full_snr_points) / sizeof(full_snr_points[0]));
    const char* profiles[] = {"awgn", "radio"};
    std::mt19937 rng(0xBEEFu);
    const PhyProfile phy = current_phy_profile();

    if (print_header) {
        std::cout
            << "profile,phy_version,protocol_version,snr_db,trials,"
            << "sample_rate,symbol_samples,alphabet,bits_per_symbol,"
            << "pilot_interval,pilot_symbol,fec_rate,legacy_compatible,"
            << "same_bitrate_as_legacy,same_channel_as_legacy,occupied_audio_bandwidth_hz,"
            << "required_audio_bandwidth_hz,net_payload_bitrate_bps,"
            << "coded_bitrate_bps,frame_duration_s,overhead_ratio,"
            << "estimated_EsN0_db,estimated_EbN0_db,processing_gain_db,"
            << "sync_fail,header_fec_fail,body_fec_fail,crc_fail,"
            << "false_lock,incomplete,converged_but_crc,"
            << "rx_raw_ser,rx_raw_ber,oracle_raw_ser,oracle_raw_ber,"
            << "per,payload_ber_accepted,header_fec_failed_blocks,"
            << "body_fec_failed_blocks,max_fec_iterations,max_syndrome_weight,"
            << "llr_mode,metric_noise_variance,mean_peak_margin,llr_saturation_rate,"
            << "pilot_count,pilot_used_for_timing,pilot_rejected_low_confidence,"
            << "pilot_mean_margin,pilot_timing_offset_rms,timing_error_rms,"
            << "span_estimate_mean,span_estimate_min,span_estimate_max,"
            << "timing_search_profile,timing_search_full_count,"
            << "timing_search_local_count,timing_search_center_count,"
            << "average_offsets_per_symbol,adaptive_llr_enabled,"
            << "adaptive_llr_scale,known_symbol_margin_median,"
            << "known_symbol_margin_p05,known_symbol_count\n";
    }
    for (const char* profile : profiles) {
        if (profile_filter != nullptr && std::string(profile) != profile_filter) continue;
        for (size_t snr_index = 0; snr_index < snr_point_count; ++snr_index) {
            const double snr_db = snr_points[snr_index];
            size_t rx_raw_symbols = 0;
            size_t rx_raw_symbol_errors = 0;
            size_t rx_raw_bits = 0;
            size_t rx_raw_bit_errors = 0;
            size_t oracle_raw_symbols = 0;
            size_t oracle_raw_symbol_errors = 0;
            size_t oracle_raw_bits = 0;
            size_t oracle_raw_bit_errors = 0;
            size_t sync_fail = 0;
            size_t header_fec_fail = 0;
            size_t body_fec_fail = 0;
            size_t crc_fail = 0;
            size_t false_lock = 0;
            size_t incomplete = 0;
            size_t converged_but_crc = 0;
            size_t packet_errors = 0;
            size_t accepted_payload_bits = 0;
            size_t accepted_payload_bit_errors = 0;
            size_t header_fec_failed_blocks = 0;
            size_t body_fec_failed_blocks = 0;
            int max_fec_iterations = 0;
            int max_syndrome_weight = 0;
            size_t frame_samples_accum = 0;
            size_t tx_bits_accum = 0;
            size_t payload_bits_accum = 0;
            size_t physical_symbol_accum = 0;
            double metric_noise_variance_sum = 0.0;
            double mean_peak_margin_sum = 0.0;
            double llr_saturation_rate_sum = 0.0;
            int metric_diag_count = 0;
            int pilot_count = 0;
            int pilot_used_for_timing = 0;
            int pilot_rejected_low_confidence = 0;
            double pilot_margin_sum = 0.0;
            double pilot_offset_rms_sum = 0.0;
            double timing_error_rms_sum = 0.0;
            double span_mean_sum = 0.0;
            double span_min = std::numeric_limits<double>::infinity();
            double span_max = -std::numeric_limits<double>::infinity();
            int timing_search_full_count = 0;
            int timing_search_local_count = 0;
            int timing_search_center_count = 0;
            double timing_search_average_offsets_sum = 0.0;
            double adaptive_llr_scale_sum = 0.0;
            double known_symbol_margin_median_sum = 0.0;
            double known_symbol_margin_p05_sum = 0.0;
            int known_symbol_count_sum = 0;

            for (int trial = 0; trial < trials_per_point; ++trial) {
                std::vector<uint8_t> payload(64);
                for (size_t i = 0; i < payload.size(); ++i) {
                    payload[i] = uint8_t((trial * 131 + int(i) * 17 + int(rng())) & 0xFF);
                }

                const std::vector<uint8_t> frame =
                    build_protected_frame(payload, PROTOCOL_MAGIC, PROTOCOL_VERSION, 0);
                const std::vector<uint8_t> tx_bits = build_frame_tx_bits(frame);
                const std::vector<uint8_t> expected_symbols = bits_to_symbols(tx_bits);
                const std::vector<int16_t> clean = encode_frame_bytes_to_pcm(frame);
                const size_t data_symbols =
                    (tx_bits.size() + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL;
                const size_t physical_symbols =
                    data_symbols + pilot_count_for_data_symbols(data_symbols) +
                    PREAMBLE_SYMBOLS + SYNC_SYMBOLS;
                frame_samples_accum += clean.size();
                tx_bits_accum += tx_bits.size();
                payload_bits_accum += payload.size() * 8;
                physical_symbol_accum += physical_symbols;

                std::vector<int16_t> impaired =
                    std::string(profile) == "radio"
                        ? apply_quality_radio_channel(clean, snr_db, &rng)
                        : add_awgn_for_snr(clean, snr_db, &rng);

                const RawLinkMetrics rx_raw =
                    measure_receiver_selected_raw_link_metrics(impaired, tx_bits,
                                                               expected_symbols);
                rx_raw_symbols += rx_raw.symbols;
                rx_raw_symbol_errors += rx_raw.symbol_errors;
                rx_raw_bits += rx_raw.bits;
                rx_raw_bit_errors += rx_raw.bit_errors;

                RawLinkMetrics oracle_raw;
                if (include_oracle_metrics) {
                    oracle_raw = measure_oracle_raw_link_metrics(impaired, tx_bits,
                                                                 expected_symbols);
                }
                oracle_raw_symbols += oracle_raw.symbols;
                oracle_raw_symbol_errors += oracle_raw.symbol_errors;
                oracle_raw_bits += oracle_raw.bits;
                oracle_raw_bit_errors += oracle_raw.bit_errors;

                std::vector<uint8_t> decoded;
                const DecodeAttemptDiagnostics diag =
                    diagnose_pcm_decode_attempt(impaired, &decoded, demod_cfg);
                header_fec_failed_blocks += size_t(diag.header_fec.failed_blocks);
                body_fec_failed_blocks += size_t(diag.body_fec.failed_blocks);
                max_fec_iterations =
                    std::max(max_fec_iterations,
                             std::max(diag.header_fec.max_iterations,
                                      diag.body_fec.max_iterations));
                max_syndrome_weight =
                    std::max(max_syndrome_weight,
                             std::max(diag.header_fec.max_syndrome_weight,
                                      diag.body_fec.max_syndrome_weight));
                metric_noise_variance_sum += diag.metric_stats.loser_variance;
                mean_peak_margin_sum += diag.metric_stats.mean_peak_margin;
                llr_saturation_rate_sum += diag.metric_stats.llr_saturation_rate;
                ++metric_diag_count;
                pilot_count += diag.timing_diag.pilot_count;
                pilot_used_for_timing += diag.timing_diag.pilot_used_for_timing;
                pilot_rejected_low_confidence +=
                    diag.timing_diag.pilot_rejected_low_confidence;
                pilot_margin_sum += diag.timing_diag.pilot_mean_margin;
                pilot_offset_rms_sum += diag.timing_diag.pilot_timing_offset_rms;
                timing_error_rms_sum += diag.timing_diag.timing_error_rms;
                span_mean_sum += diag.timing_diag.span_estimate_mean;
                span_min = std::min(span_min, diag.timing_diag.span_estimate_min);
                span_max = std::max(span_max, diag.timing_diag.span_estimate_max);
                timing_search_full_count += diag.timing_diag.timing_search_full_count;
                timing_search_local_count += diag.timing_diag.timing_search_local_count;
                timing_search_center_count += diag.timing_diag.timing_search_center_count;
                timing_search_average_offsets_sum +=
                    diag.timing_diag.average_offsets_per_symbol;
                adaptive_llr_scale_sum += diag.demod_cfg_used.adaptive_llr_scale;
                known_symbol_margin_median_sum +=
                    diag.demod_cfg_used.known_symbol_margin_median;
                known_symbol_margin_p05_sum += diag.demod_cfg_used.known_symbol_margin_p05;
                known_symbol_count_sum += diag.demod_cfg_used.known_symbol_count;

                if (!diag.ok || decoded != payload) {
                    ++packet_errors;
                    switch (diag.cause) {
                        case DecodeFailureCause::Sync: ++sync_fail; break;
                        case DecodeFailureCause::HeaderFec: ++header_fec_fail; break;
                        case DecodeFailureCause::BodyFec: ++body_fec_fail; break;
                        case DecodeFailureCause::Crc: ++crc_fail; break;
                        case DecodeFailureCause::FalseLock: ++false_lock; break;
                        case DecodeFailureCause::Incomplete: ++incomplete; break;
                        case DecodeFailureCause::ConvergedButCrc:
                            ++converged_but_crc;
                            ++crc_fail;
                            break;
                        case DecodeFailureCause::None:
                            ++crc_fail;
                            break;
                    }
                } else {
                    accepted_payload_bits += payload.size() * 8;
                    accepted_payload_bit_errors += count_payload_bit_errors(payload, decoded);
                }
            }

            const double rx_raw_ser =
                rx_raw_symbols ? double(rx_raw_symbol_errors) / double(rx_raw_symbols) : 1.0;
            const double rx_raw_ber =
                rx_raw_bits ? double(rx_raw_bit_errors) / double(rx_raw_bits) : 0.5;
            const double oracle_raw_ser =
                oracle_raw_symbols
                    ? double(oracle_raw_symbol_errors) / double(oracle_raw_symbols)
                    : 1.0;
            const double oracle_raw_ber =
                oracle_raw_bits
                    ? double(oracle_raw_bit_errors) / double(oracle_raw_bits)
                    : 0.5;
            const double per = double(packet_errors) / double(trials_per_point);
            const double payload_ber =
                accepted_payload_bits
                    ? double(accepted_payload_bit_errors) / double(accepted_payload_bits)
                    : 0.0;
            const double frame_duration_s =
                frame_samples_accum
                    ? double(frame_samples_accum) /
                          double(trials_per_point * SAMPLE_RATE)
                    : 0.0;
            const double net_payload_bitrate =
                frame_duration_s > 0.0
                    ? double(payload_bits_accum) / double(trials_per_point) / frame_duration_s
                    : 0.0;
            const double coded_bitrate =
                frame_duration_s > 0.0
                    ? double(tx_bits_accum) / double(trials_per_point) / frame_duration_s
                    : 0.0;
            const double carried_raw_bits =
                double(physical_symbol_accum) * double(BITS_PER_SYMBOL) /
                double(trials_per_point);
            const double useful_payload_bits =
                double(payload_bits_accum) / double(trials_per_point);
            const double overhead_ratio =
                carried_raw_bits > 0.0
                    ? std::max(0.0, (carried_raw_bits - useful_payload_bits) /
                                        carried_raw_bits)
                    : 0.0;
            const double estimated_ebn0 =
                net_payload_bitrate > 0.0
                    ? snr_db + 10.0 * std::log10(phy.occupied_audio_bandwidth_hz /
                                                 net_payload_bitrate)
                    : snr_db;
            const double processing_gain =
                coded_bitrate > 0.0
                    ? 10.0 * std::log10(phy.occupied_audio_bandwidth_hz /
                                        coded_bitrate)
                    : 0.0;
            const double metric_noise_variance =
                metric_noise_variance_sum / double(std::max(1, metric_diag_count));
            const double mean_peak_margin =
                mean_peak_margin_sum / double(std::max(1, metric_diag_count));
            const double llr_saturation_rate =
                llr_saturation_rate_sum / double(std::max(1, metric_diag_count));
            const double pilot_mean_margin =
                pilot_margin_sum / double(std::max(1, metric_diag_count));
            const double pilot_timing_offset_rms =
                pilot_offset_rms_sum / double(std::max(1, metric_diag_count));
            const double timing_error_rms =
                timing_error_rms_sum / double(std::max(1, metric_diag_count));
            const double span_estimate_mean =
                span_mean_sum / double(std::max(1, metric_diag_count));
            const double average_offsets_per_symbol =
                timing_search_average_offsets_sum /
                double(std::max(1, metric_diag_count));
            const double adaptive_llr_scale =
                adaptive_llr_scale_sum / double(std::max(1, metric_diag_count));
            const double known_symbol_margin_median =
                known_symbol_margin_median_sum / double(std::max(1, metric_diag_count));
            const double known_symbol_margin_p05 =
                known_symbol_margin_p05_sum / double(std::max(1, metric_diag_count));
            if (!std::isfinite(span_min)) span_min = 0.0;
            if (!std::isfinite(span_max)) span_max = 0.0;

            std::cout << profile << ","
                      << phy.phy_version << ","
                      << phy.protocol_version << ","
                      << snr_db << ","
                      << trials_per_point << ","
                      << phy.sample_rate << ","
                      << phy.symbol_samples << ","
                      << phy.alphabet << ","
                      << phy.bits_per_symbol << ","
                      << phy.pilot_interval_symbols << ","
                      << phy.pilot_symbol << ","
                      << phy.fec_rate << ","
                      << (phy.legacy_compatible ? 1 : 0) << ","
                      << (phy.same_bitrate_as_legacy ? 1 : 0) << ","
                      << (phy.same_channel_as_legacy ? 1 : 0) << ","
                      << phy.occupied_audio_bandwidth_hz << ","
                      << phy.required_audio_bandwidth_hz << ","
                      << net_payload_bitrate << ","
                      << coded_bitrate << ","
                      << frame_duration_s << ","
                      << overhead_ratio << ","
                      << snr_db << ","
                      << estimated_ebn0 << ","
                      << processing_gain << ","
                      << sync_fail << ","
                      << header_fec_fail << ","
                      << body_fec_fail << ","
                      << crc_fail << ","
                      << false_lock << ","
                      << incomplete << ","
                      << converged_but_crc << ","
                      << rx_raw_ser << ","
                      << rx_raw_ber << ","
                      << oracle_raw_ser << ","
                      << oracle_raw_ber << ","
                      << per << ","
                      << payload_ber << ","
                      << header_fec_failed_blocks << ","
                      << body_fec_failed_blocks << ","
                      << max_fec_iterations << ","
                      << max_syndrome_weight << ","
                      << llr_mode_name(demod_cfg) << ","
                      << metric_noise_variance << ","
                      << mean_peak_margin << ","
                      << llr_saturation_rate << ","
                      << pilot_count << ","
                      << pilot_used_for_timing << ","
                      << pilot_rejected_low_confidence << ","
                      << pilot_mean_margin << ","
                      << pilot_timing_offset_rms << ","
                      << timing_error_rms << ","
                      << span_estimate_mean << ","
                      << span_min << ","
                      << span_max << ","
                      << timing_search_profile_name(g_receiver_options.timing_search_profile) << ","
                      << timing_search_full_count << ","
                      << timing_search_local_count << ","
                      << timing_search_center_count << ","
                      << average_offsets_per_symbol << ","
                      << (demod_cfg.use_adaptive_llr ? 1 : 0) << ","
                      << adaptive_llr_scale << ","
                      << known_symbol_margin_median << ","
                      << known_symbol_margin_p05 << ","
                      << known_symbol_count_sum << "\n";
        }
    }
}

static void run_pcm_debug_measurement(const std::string& profile,
                                      double snr_db,
                                      int trials) {
    if (profile != "awgn" && profile != "radio") {
        throw std::runtime_error("measure-pcm-debug --profile must be awgn or radio");
    }
    if (trials <= 0) throw std::runtime_error("measure-pcm-debug trials must be positive");

    std::mt19937 rng(0xBEEFu);
    const DemodConfig demod_cfg = receiver_demod_config();
    std::cout << "measure-pcm-debug profile=" << profile
              << " snr_db=" << snr_db
              << " trials=" << trials
              << " timing_search="
              << timing_search_profile_name(g_receiver_options.timing_search_profile)
              << " adaptive_llr=" << (demod_cfg.use_adaptive_llr ? 1 : 0)
              << " adaptive_clock_tracking="
              << (g_receiver_options.adaptive_clock_tracking_enabled ? 1 : 0)
              << " rng_seed=0xBEEF\n";

    for (int trial = 0; trial < trials; ++trial) {
        std::vector<uint8_t> payload(64);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = uint8_t((trial * 131 + int(i) * 17 + int(rng())) & 0xFF);
        }

        const std::vector<uint8_t> frame =
            build_protected_frame(payload, PROTOCOL_MAGIC, PROTOCOL_VERSION, 0);
        const std::vector<uint8_t> tx_bits = build_frame_tx_bits(frame);
        const std::vector<uint8_t> expected_symbols = bits_to_symbols(tx_bits);
        const std::vector<int16_t> clean = encode_frame_bytes_to_pcm(frame);
        const std::vector<int16_t> impaired =
            profile == "radio" ? apply_quality_radio_channel(clean, snr_db, &rng)
                                : add_awgn_for_snr(clean, snr_db, &rng);

        std::vector<uint8_t> decoded;
        const DecodeAttemptDiagnostics diag =
            diagnose_pcm_decode_attempt(impaired, &decoded, demod_cfg);
        const bool payload_ok = diag.ok && decoded == payload;
        if (payload_ok) {
            std::cout << "trial=" << trial << " status=ok"
                      << " sync_score=" << diag.sync_score
                      << " selected_candidate_span=" << diag.selected_candidate_span
                      << " timing_search_full_count="
                      << diag.timing_diag.timing_search_full_count
                      << " timing_search_local_count="
                      << diag.timing_diag.timing_search_local_count
                      << " timing_search_center_count="
                      << diag.timing_diag.timing_search_center_count
                      << " average_offsets_per_symbol="
                      << diag.timing_diag.average_offsets_per_symbol
                      << " adaptive_llr_scale="
                      << diag.demod_cfg_used.adaptive_llr_scale
                      << " known_symbol_margin_median="
                      << diag.demod_cfg_used.known_symbol_margin_median
                      << " known_symbol_count="
                      << diag.demod_cfg_used.known_symbol_count
                      << " estimated_clock_ppm="
                      << diag.timing_diag.estimated_clock_ppm
                      << " clock_scale=" << diag.timing_diag.clock_scale
                      << " clock_fit_error_rms_samples="
                      << diag.timing_diag.clock_fit_error_rms_samples
                      << " clock_fit_points="
                      << diag.timing_diag.clock_fit_points
                      << " timing_error_before_rms="
                      << diag.timing_diag.timing_error_before_rms
                      << " timing_error_after_rms="
                      << diag.timing_diag.timing_error_after_rms
                      << "\n";
            continue;
        }

        const std::vector<uint8_t> header_bits(
            tx_bits.begin(),
            tx_bits.begin() + std::ptrdiff_t(std::min(tx_bits.size(),
                                                      size_t(FEC_CODEWORD_BITS))));
        const std::vector<uint8_t> header_symbols(
            expected_symbols.begin(),
            expected_symbols.begin() +
                std::ptrdiff_t(std::min(expected_symbols.size(),
                                        size_t(FEC_CODEWORD_BITS / BITS_PER_SYMBOL))));
        const RawLinkMetrics header_raw =
            measure_receiver_selected_raw_link_metrics(impaired, header_bits, header_symbols);
        const RawLinkMetrics oracle_raw =
            measure_oracle_raw_link_metrics(impaired, tx_bits, expected_symbols);
        const bool oracle_candidate_would_succeed = oracle_raw.bit_errors == 0;

        std::cout << "trial=" << trial << " status=fail"
                  << " cause=" << decode_failure_cause_name(diag.cause) << "\n";
        std::cout << "reproduction rng_seed=0xBEEF trial_index=" << trial
                  << " profile=" << profile
                  << " snr_db=" << snr_db << "\n";
        std::cout << "sync_locked=" << (diag.sync_locked ? 1 : 0)
                  << " sync_score=" << diag.sync_score
                  << " estimated_symbol_span=" << diag.estimated_symbol_span
                  << " selected_candidate_span=" << diag.selected_candidate_span
                  << " estimated_sample_rate_error_percent="
                  << (diag.selected_candidate_span > 0.0
                          ? 100.0 * (diag.selected_candidate_span / NOMINAL_SPAN - 1.0)
                          : 0.0)
                  << "\n";
        std::cout << "header_raw_symbol_errors=" << header_raw.symbol_errors
                  << " header_raw_symbols=" << header_raw.symbols
                  << " header_raw_bit_errors=" << header_raw.bit_errors
                  << " header_raw_bits=" << header_raw.bits << "\n";
        std::cout << "header_fec_all_blocks_ok=" << (diag.header_fec.all_blocks_ok ? 1 : 0)
                  << " header_fec_failed_blocks=" << diag.header_fec.failed_blocks
                  << " header_fec_block_count=" << diag.header_fec.block_count
                  << " header_fec_max_iterations=" << diag.header_fec.max_iterations
                  << " header_fec_max_syndrome_weight="
                  << diag.header_fec.max_syndrome_weight
                  << " header_fec_total_syndrome_weight="
                  << diag.header_fec.total_syndrome_weight << "\n";
        std::cout << "llr_mode=" << llr_mode_name(demod_cfg)
                  << " metric_noise_variance=" << diag.metric_stats.loser_variance
                  << " mean_peak_margin=" << diag.metric_stats.mean_peak_margin
                  << " llr_saturation_rate=" << diag.metric_stats.llr_saturation_rate
                  << " adaptive_llr_scale=" << diag.demod_cfg_used.adaptive_llr_scale
                  << " known_symbol_margin_median="
                  << diag.demod_cfg_used.known_symbol_margin_median
                  << " known_symbol_count=" << diag.demod_cfg_used.known_symbol_count
                  << "\n";
        std::cout << "pilot_count=" << diag.timing_diag.pilot_count
                  << " pilot_used_for_timing=" << diag.timing_diag.pilot_used_for_timing
                  << " pilot_rejected_low_confidence="
                  << diag.timing_diag.pilot_rejected_low_confidence
                  << " pilot_mean_margin=" << diag.timing_diag.pilot_mean_margin
                  << " pilot_timing_offset_rms="
                  << diag.timing_diag.pilot_timing_offset_rms
                  << " timing_error_rms=" << diag.timing_diag.timing_error_rms
                  << " span_estimate_mean=" << diag.timing_diag.span_estimate_mean
                  << " span_estimate_min=" << diag.timing_diag.span_estimate_min
                  << " span_estimate_max=" << diag.timing_diag.span_estimate_max
                  << " timing_search_full_count="
                  << diag.timing_diag.timing_search_full_count
                  << " timing_search_local_count="
                  << diag.timing_diag.timing_search_local_count
                  << " timing_search_center_count="
                  << diag.timing_diag.timing_search_center_count
                  << " average_offsets_per_symbol="
                  << diag.timing_diag.average_offsets_per_symbol
                  << " estimated_clock_ppm=" << diag.timing_diag.estimated_clock_ppm
                  << " clock_scale=" << diag.timing_diag.clock_scale
                  << " clock_fit_error_rms_samples="
                  << diag.timing_diag.clock_fit_error_rms_samples
                  << " clock_fit_points=" << diag.timing_diag.clock_fit_points
                  << " timing_error_before_rms="
                  << diag.timing_diag.timing_error_before_rms
                  << " timing_error_after_rms="
                  << diag.timing_diag.timing_error_after_rms
                  << "\n";
        std::cout << "header_decoded_bytes_hex="
                  << bytes_to_hex_string(diag.header_decoded_bytes) << "\n";
        std::cout << "oracle_candidate_would_succeed="
                  << (oracle_candidate_would_succeed ? 1 : 0)
                  << " oracle_raw_symbol_errors=" << oracle_raw.symbol_errors
                  << " oracle_raw_symbols=" << oracle_raw.symbols
                  << " oracle_raw_bit_errors=" << oracle_raw.bit_errors
                  << " oracle_raw_bits=" << oracle_raw.bits << "\n";
        return;
    }

    std::cout << "no_failure_reproduced=1\n";
}

static void run_pcm_sweep_measurement(const std::string& profile,
                                      int trials) {
    const double sweep_snr_points[] = {24.0, 18.0, 15.0, 12.0, 9.0, 6.0, 3.0, 0.0, -3.0};
    run_pcm_quality_measurement(trials, receiver_demod_config(), profile.c_str(),
                                sweep_snr_points,
                                sizeof(sweep_snr_points) / sizeof(sweep_snr_points[0]),
                                true, false);
}

static void run_compare_demod_measurement(const std::string& profile,
                                          double snr_db,
                                          int trials) {
    run_pcm_quality_measurement(trials, fixed_llr_demod_config(), profile.c_str(),
                                &snr_db, 1, true, false);
    run_pcm_quality_measurement(trials, calibrated_llr_demod_config(), profile.c_str(),
                                &snr_db, 1, false, false);
}

static void run_correlation_benchmark(int symbols) {
    if (symbols <= 0) throw std::runtime_error("bench-correlation symbols must be positive");

    std::vector<uint8_t> payload(96);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = uint8_t((int(i) * 37 + 11) & 0xFF);
    }
    const std::vector<int16_t> pcm = encode_payload_to_pcm(payload);

    std::vector<double> positions;
    const double data_start = double(PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * NOMINAL_SPAN;
    const double data_end = std::max(data_start, double(pcm.size()) - 0.25 * SAMPLE_RATE);
    for (double pos = data_start; pos + NOMINAL_SPAN < data_end; pos += NOMINAL_SPAN) {
        positions.push_back(pos);
    }
    if (positions.empty()) {
        throw std::runtime_error("bench-correlation could not find benchmark symbols");
    }

    reset_circular_chirp_correlation_diagnostics();
    volatile int symbol_accumulator = 0;
    const std::clock_t start = std::clock();
    for (int i = 0; i < symbols; ++i) {
        const double pos = positions[size_t(i % int(positions.size()))];
        const SymbolMetrics m =
            decode_symbol_metrics_at(pcm, pos, NOMINAL_SPAN, false, nullptr,
                                     nullptr, -1, TimingSearchProfile::CenterOnly,
                                     nullptr);
        symbol_accumulator += m.best_symbol;
    }
    const std::clock_t end = std::clock();
    const double elapsed_s = double(end - start) / double(CLOCKS_PER_SEC);
    const double total_ms = elapsed_s * 1000.0;
    const double average_us =
        symbols > 0 ? elapsed_s * 1000000.0 / double(symbols) : 0.0;
    const FftCorrelationDiagnostics diag =
        circular_chirp_correlation_diagnostics();

    std::cout << "symbols_decoded=" << symbols
              << " total_decode_ms=" << total_ms
              << " average_microseconds_per_symbol=" << average_us
              << " correlation_calls=" << diag.correlation_calls
              << " precomputed_correlation_calls="
              << diag.precomputed_correlation_calls
              << " sample_ffts_computed=" << diag.sample_ffts_computed
              << " base_ffts_computed=" << diag.base_ffts_computed
              << " base_fft_cache_hits=" << diag.base_fft_cache_hits
              << " base_fft_cache_misses=" << diag.base_fft_cache_misses
              << " base_fft_cache_entries=" << diag.base_fft_cache_entries
              << " base_fft_cache_capacity=" << diag.base_fft_cache_capacity
              << " symbol_accumulator=" << symbol_accumulator
              << "\n";
}

static void require_true(bool ok, const std::string& name) {
    if (!ok) throw std::runtime_error("Selftest failed: " + name);
    std::cerr << "[PASS] " << name << "\n";
}

static bool feed_stream_with_scanner(const std::vector<int16_t>& stream,
                                     size_t chunk_samples,
                                     std::vector<uint8_t>* payload,
                                     size_t* max_buffer,
                                     bool* saw_need_more,
                                     bool* saw_invalid) {
    std::vector<int16_t> buffer;
    *max_buffer = 0;
    if (saw_need_more) *saw_need_more = false;
    if (saw_invalid) *saw_invalid = false;

    for (size_t pos = 0; pos < stream.size(); pos += chunk_samples) {
        const size_t end = std::min(stream.size(), pos + chunk_samples);
        buffer.insert(buffer.end(), stream.begin() + std::ptrdiff_t(pos),
                      stream.begin() + std::ptrdiff_t(end));
        *max_buffer = std::max(*max_buffer, buffer.size());

        for (int iter = 0; iter < 8; ++iter) {
            const StreamScanResult r = scan_pcm_window_for_frame(buffer);
            if (r.status == StreamScanStatus::FrameDecoded) {
                *payload = r.payload;
                if (r.discard_prefix_samples > 0) {
                    const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                    buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
                }
                *max_buffer = std::max(*max_buffer, buffer.size());
                return true;
            }
            if (r.status == StreamScanStatus::NeedMoreSamples && saw_need_more) {
                *saw_need_more = true;
            }
            if (r.status == StreamScanStatus::InvalidFrameRejected && saw_invalid) {
                *saw_invalid = true;
            }

            if (r.discard_prefix_samples == 0) break;
            const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
            buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            *max_buffer = std::max(*max_buffer, buffer.size());
            if (buffer.empty()) break;
        }
    }

    for (int iter = 0; iter < 16 && !buffer.empty(); ++iter) {
        const StreamScanResult r = scan_pcm_window_for_frame(buffer);
        if (r.status == StreamScanStatus::FrameDecoded) {
            *payload = r.payload;
            return true;
        }
        if (r.status == StreamScanStatus::NeedMoreSamples && saw_need_more) *saw_need_more = true;
        if (r.status == StreamScanStatus::InvalidFrameRejected && saw_invalid) *saw_invalid = true;
        if (r.discard_prefix_samples == 0) break;
        const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
        buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
        *max_buffer = std::max(*max_buffer, buffer.size());
    }
    return false;
}

static void run_selftest() {
    {
        const std::vector<uint8_t> bytes = {0x00, 0x5A, 0xC3, 0xFF, 0x10};
        require_true(bits_to_bytes(bytes_to_bits(bytes)) == bytes, "bytes/bits roundtrip");
    }
    {
        for (int i = 0; i < ALPHABET; ++i) {
            require_true(gray_to_binary4(binary_to_gray4(uint8_t(i))) == i, "gray roundtrip");
        }
    }
    {
        std::vector<uint8_t> hard(257);
        for (size_t i = 0; i < hard.size(); ++i) hard[i] = uint8_t(i & 1U);
        require_true(deinterleave(interleave(hard)) == hard, "hard interleaver roundtrip");

        std::vector<double> soft(257);
        for (size_t i = 0; i < soft.size(); ++i) soft[i] = double(i) * 0.25 - 12.0;
        require_true(deinterleave_soft(interleave_soft(soft)) == soft, "soft interleaver roundtrip");
    }
    {
        SymbolMetrics m;
        for (double& v : m.metric) v = 0.0;
        const uint8_t binary_symbol = 0x0A;
        const uint8_t raw_symbol = binary_to_gray4(binary_symbol);
        m.metric[size_t(raw_symbol)] = 1.0;
        finalize_best_scores(&m);

        const std::array<double, BITS_PER_SYMBOL> fixed =
            symbol_metrics_to_llr(m, fixed_llr_demod_config(), nullptr);
        for (int bit = 0; bit < BITS_PER_SYMBOL; ++bit) {
            const int expected = (binary_symbol >> (BITS_PER_SYMBOL - 1 - bit)) & 1;
            require_true(expected == 0 ? fixed[size_t(bit)] > 0.0
                                       : fixed[size_t(bit)] < 0.0,
                         "fixed LLR ideal sign");
        }

        MetricStats stats;
        metric_stats_observe_known_symbol(&stats, m, raw_symbol);
        require_true(stats.loser_variance > 0.0, "MetricStats positive variance");
        DemodConfig calibrated = calibrated_llr_demod_config();
        calibrated.llr_clip = 2.0;
        const std::array<double, BITS_PER_SYMBOL> cal =
            symbol_metrics_to_llr(m, calibrated, &stats);
        for (double v : cal) {
            require_true(std::isfinite(v), "calibrated LLR finite");
            require_true(std::abs(v) <= calibrated.llr_clip, "calibrated LLR clamp");
        }

        TimingLoopConfig timing_cfg;
        require_true(!pilot_is_strong_for_timing(0.0, 0.25, timing_cfg),
                     "weak pilot rejected");
        require_true(pilot_is_strong_for_timing(timing_cfg.pilot_confidence_threshold + 0.1,
                                                0.25, timing_cfg),
                     "strong pilot accepted");
        require_true(!pilot_is_strong_for_timing(timing_cfg.pilot_confidence_threshold + 0.1,
                                                 timing_cfg.max_timing_update + 1.0,
                                                 timing_cfg),
                     "pilot timing outlier rejected");
    }
    {
        require_true(LDPCCodec::has_unique_nonzero_columns(), "FEC unique nonzero columns");

        std::vector<uint8_t> info(3 * FEC_INFO_BITS);
        for (size_t i = 0; i < info.size(); ++i) info[i] = uint8_t(((i * 7U) + 3U) & 1U);
        const std::vector<uint8_t> coded = fec_encode_bits(info);

        std::vector<double> llr;
        llr.reserve(coded.size());
        for (uint8_t b : coded) llr.push_back((b & 1) ? -5.0 : 5.0);
        std::vector<uint8_t> decoded = fec_decode_bits_from_llr(llr);
        decoded.resize(info.size());
        require_true(decoded == info, "FEC no errors");

        decoded = fec_decode_bits_hard(coded);
        decoded.resize(info.size());
        require_true(decoded == info, "FEC hard API no errors");

        for (int pos : {0, 1, 7, 31, 63, 64, 91, 127}) {
            std::vector<double> damaged = llr;
            damaged[size_t(pos)] = -damaged[size_t(pos)];
            decoded = fec_decode_bits_from_llr(damaged);
            decoded.resize(FEC_INFO_BITS);
            std::vector<uint8_t> first_info(info.begin(), info.begin() + FEC_INFO_BITS);
            require_true(decoded == first_info, "FEC single-bit correction");
        }

        std::vector<double> damaged = llr;
        damaged[0] = -damaged[0];
        damaged[17] = -damaged[17];
        decoded = fec_decode_bits_from_llr(damaged);
        decoded.resize(FEC_INFO_BITS);
        std::vector<uint8_t> first_info(info.begin(), info.begin() + FEC_INFO_BITS);
        require_true(decoded == first_info, "FEC selected two-bit correction");

        damaged = llr;
        for (int pos : {0, 17, 31, 63, 64, 91}) {
            damaged[size_t(pos)] = coded[size_t(pos)] ? 0.35 : -0.35;
        }
        decoded = fec_decode_bits_from_llr(damaged);
        decoded.resize(FEC_INFO_BITS);
        require_true(decoded == first_info, "FEC soft BP low-confidence correction");
    }
    {
        std::mt19937 rng(12345);
        std::vector<uint8_t> payload(48);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        const std::vector<int16_t> pcm = encode_payload_to_pcm(payload);
        std::vector<uint8_t> decoded;
        require_true(decode_payload_from_pcm(pcm, &decoded) && decoded == payload, "clean modem channel");

        std::vector<int16_t> with_silence(900, 0);
        with_silence.insert(with_silence.end(), pcm.begin(), pcm.end());
        require_true(decode_payload_from_pcm(with_silence, &decoded) && decoded == payload, "leading silence");

        const double factors[] = {0.98, 0.99, 1.01, 1.02};
        for (double factor : factors) {
            const std::vector<int16_t> scaled = resample_pcm(pcm, factor);
            require_true(decode_payload_from_pcm(scaled, &decoded) && decoded == payload,
                         "time scaling");
        }
    }
    {
        std::mt19937 rng(2222);
        std::vector<uint8_t> payload(40);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        append_silence(stream, 1800);
        append_white_noise(stream, 2400, 250.0, rng);
        append_unrelated_chirp_burst(stream, 3, rng);
        append_white_noise(stream, 2200, 500.0, rng);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.95);
        append_unrelated_chirp_burst(stream, 2, rng);
        append_white_noise(stream, 1200, 300.0, rng);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 512, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload,
                     "sliding_window_stream_receiver");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "sliding receiver bounded buffer");
    }
    {
        std::mt19937 rng(3333);
        std::vector<uint8_t> payload(32);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        for (int second = 0; second < 120; ++second) {
            append_white_noise(stream, SAMPLE_RATE, 180.0, rng);
            if ((second % 40) == 7) append_unrelated_chirp_burst(stream, 1, rng);
        }
        append_silence(stream, 1600);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.90);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 4096, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload,
                     "long_idle_then_valid_frame");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "long idle bounded buffer");
    }
    {
        std::mt19937 rng(4444);
        std::vector<int16_t> stream;
        append_silence(stream, SAMPLE_RATE);
        append_white_noise(stream, SAMPLE_RATE * 3, 260.0, rng);
        append_unrelated_chirp_burst(stream, 4, rng);
        append_white_noise(stream, SAMPLE_RATE, 350.0, rng);
        append_fake_wrong_sync_transmission(stream, rng);
        append_white_noise(stream, SAMPLE_RATE, 220.0, rng);
        append_fake_wrong_magic_transmission(stream, rng);
        append_white_noise(stream, SAMPLE_RATE, 200.0, rng);
        append_corrupted_frame_like_burst(stream, rng);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(!feed_stream_with_scanner(stream, 4096, &decoded, &max_buffer,
                                              &saw_need_more, &saw_invalid),
                     "stream_no_valid_frame");
        require_true(decoded.empty(), "stream no valid payload empty");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "no-frame bounded buffer");
    }
    {
        std::mt19937 rng(5555);
        std::vector<uint8_t> payload(44);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);
        const std::vector<int16_t> frame = encode_payload_to_pcm(payload);
        const size_t partial_len =
            size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS + 40) * SYMBOL_SAMPLES);

        std::vector<int16_t> prefix;
        append_white_noise(prefix, 2400, 220.0, rng);
        std::vector<int16_t> partial = prefix;
        partial.insert(partial.end(), frame.begin(),
                       frame.begin() + std::ptrdiff_t(std::min(partial_len, frame.size())));

        std::vector<int16_t> buffer;
        std::vector<uint8_t> decoded;
        bool got_frame = false;
        bool saw_need_more = false;
        size_t max_buffer = 0;
        for (size_t pos = 0; pos < partial.size(); pos += 512) {
            const size_t end = std::min(partial.size(), pos + size_t(512));
            buffer.insert(buffer.end(), partial.begin() + std::ptrdiff_t(pos),
                          partial.begin() + std::ptrdiff_t(end));
            max_buffer = std::max(max_buffer, buffer.size());
            const StreamScanResult r = scan_pcm_window_for_frame(buffer);
            if (r.status == StreamScanStatus::FrameDecoded) got_frame = true;
            if (r.status == StreamScanStatus::NeedMoreSamples) saw_need_more = true;
            if (r.discard_prefix_samples > 0) {
                const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            }
        }
        require_true(!got_frame && saw_need_more, "stream_incomplete_frame_tail");

        for (size_t pos = std::min(partial_len, frame.size()); pos < frame.size(); pos += 512) {
            const size_t end = std::min(frame.size(), pos + size_t(512));
            buffer.insert(buffer.end(), frame.begin() + std::ptrdiff_t(pos),
                          frame.begin() + std::ptrdiff_t(end));
            max_buffer = std::max(max_buffer, buffer.size());
            for (int iter = 0; iter < 8; ++iter) {
                const StreamScanResult r = scan_pcm_window_for_frame(buffer);
                if (r.status == StreamScanStatus::FrameDecoded) {
                    decoded = r.payload;
                    got_frame = true;
                    break;
                }
                if (r.discard_prefix_samples == 0) break;
                const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            }
            if (got_frame) break;
        }
        require_true(got_frame && decoded == payload, "stream incomplete tail completed");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "incomplete stream bounded buffer");
    }
    {
        std::mt19937 rng(6666);
        std::vector<uint8_t> payload(36);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        append_fake_wrong_magic_transmission(stream, rng);
        append_silence(stream, 1400);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.92);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 512, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload && saw_invalid,
                     "wrong frame rejected");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "wrong-frame bounded buffer");
    }

    std::cerr << "All self-tests passed.\n";
}

#ifndef CHIRP_MODEM_NO_MAIN
int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        g_receiver_options = parse_receiver_options_from_cli(argc, argv, &args);

        if (args.size() == 1 && args[0] == "selftest") {
            run_selftest();
            return 0;
        }

        if (!args.empty() && args[0] == "measure-metric") {
            const int trials = args.size() >= 2 ? std::atoi(args[1].c_str()) : 500;
            std::cerr << "NOTE: measure-metric is a synthetic metric-channel model, "
                      << "not the real PCM waveform receiver.\n";
            run_statistical_quality_measurement(trials);
            return 0;
        }

        if (!args.empty() && args[0] == "measure") {
            const int trials = args.size() >= 2 ? std::atoi(args[1].c_str()) : 500;
            std::cerr << "WARNING: 'measure' is a legacy alias for synthetic "
                      << "metric-channel measurement; use 'measure-pcm' for "
                      << "waveform/PCM receiver measurement.\n";
            run_statistical_quality_measurement(trials);
            return 0;
        }

        if (!args.empty() && args[0] == "measure-pcm") {
            const int trials = args.size() >= 2 ? std::atoi(args[1].c_str()) : 2;
            print_not_same_bitrate_notice();
            run_pcm_quality_measurement(trials, receiver_demod_config());
            return 0;
        }

        if (!args.empty() && args[0] == "measure-pcm-debug") {
            std::string profile = "radio";
            double snr_db = 24.0;
            int trials = 20;
            for (size_t i = 1; i < args.size(); ++i) {
                const std::string& arg = args[i];
                if (arg == "--profile" && i + 1 < args.size()) {
                    profile = args[++i];
                } else if (arg == "--snr" && i + 1 < args.size()) {
                    snr_db = std::atof(args[++i].c_str());
                } else if (arg == "--trials" && i + 1 < args.size()) {
                    trials = std::atoi(args[++i].c_str());
                } else {
                    throw std::runtime_error("Unknown measure-pcm-debug argument");
                }
            }
            run_pcm_debug_measurement(profile, snr_db, trials);
            return 0;
        }

        if (!args.empty() && args[0] == "measure-pcm-sweep") {
            std::string profile = "radio";
            int trials = 100;
            for (size_t i = 1; i < args.size(); ++i) {
                const std::string& arg = args[i];
                if (arg == "--profile" && i + 1 < args.size()) {
                    profile = args[++i];
                } else if (arg == "--trials" && i + 1 < args.size()) {
                    trials = std::atoi(args[++i].c_str());
                } else {
                    throw std::runtime_error("Unknown measure-pcm-sweep argument");
                }
            }
            run_pcm_sweep_measurement(profile, trials);
            return 0;
        }

        if (!args.empty() && args[0] == "compare-demod") {
            std::string profile = "radio";
            double snr_db = 9.0;
            int trials = 100;
            for (size_t i = 1; i < args.size(); ++i) {
                const std::string& arg = args[i];
                if (arg == "--profile" && i + 1 < args.size()) {
                    profile = args[++i];
                } else if (arg == "--snr" && i + 1 < args.size()) {
                    snr_db = std::atof(args[++i].c_str());
                } else if (arg == "--trials" && i + 1 < args.size()) {
                    trials = std::atoi(args[++i].c_str());
                } else {
                    throw std::runtime_error("Unknown compare-demod argument");
                }
            }
            run_compare_demod_measurement(profile, snr_db, trials);
            return 0;
        }

        if (!args.empty() && args[0] == "bench-correlation") {
            const int symbols = args.size() >= 2 ? std::atoi(args[1].c_str()) : 2000;
            run_correlation_benchmark(symbols);
            return 0;
        }

        if (args.size() != 3) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " enc input.bin output.pcm\n"
                      << "  " << argv[0] << " dec input.pcm output.bin\n"
                      << "  " << argv[0] << " selftest\n"
                      << "  " << argv[0]
                      << " [--timing-search=full|local|center] <command> ...\n"
                      << "  " << argv[0]
                      << " [--rx-profile=legacy|robust] <command> ...\n"
                      << "  " << argv[0]
                      << " [--legacy-receiver|--robust-receiver] <command> ...\n"
                      << "  " << argv[0]
                      << " [--adaptive-llr] [--llr-scale X] <command> ...\n"
                      << "  " << argv[0]
                      << " [--adaptive-clock-tracking] <command> ...\n"
                      << "  " << argv[0]
                      << " [--adaptive-channel-templates|--no-adaptive-channel-templates]"
                      << " <command> ...\n"
                      << "  " << argv[0]
                      << " [--weighted-correlation|--no-weighted-correlation]"
                      << " <command> ...\n"
                      << "  " << argv[0] << " [--rx-diagnostics] dec input.pcm output.bin\n"
                      << "  " << argv[0] << " measure-metric [trials-per-snr]\n"
                      << "  " << argv[0] << " measure [trials-per-snr]  # legacy alias\n"
                      << "  " << argv[0] << " measure-pcm [trials-per-snr]\n"
                      << "  " << argv[0] << " measure-pcm-debug --profile radio --snr 24 --trials 20\n"
                      << "  " << argv[0] << " measure-pcm-sweep --profile radio --trials 100\n"
                      << "  " << argv[0] << " compare-demod --profile radio --snr 9 --trials 100\n"
                      << "  " << argv[0] << " bench-correlation [symbols]\n";
            return 1;
        }

        const std::string& mode = args[0];
        if (mode == "enc") {
            encode_file(args[1], args[2]);
        } else if (mode == "dec") {
            decode_file(args[1], args[2], g_receiver_options.rx_diagnostics_enabled);
        } else {
            throw std::runtime_error("Mode must be enc or dec");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
#endif  // CHIRP_MODEM_NO_MAIN
