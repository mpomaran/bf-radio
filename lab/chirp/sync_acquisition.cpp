#include "lab/chirp/sync_acquisition.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "lab/chirp/config.h"
#include "lab/chirp/sample_view.h"
#include "lab/chirp/waveform.h"

namespace chirp {
namespace sync {
namespace {

using config::NOMINAL_SPAN;
using config::PREAMBLE_SYMBOLS;
using config::SYMBOL_SAMPLES;
using config::SYNC_SYMBOLS;

void progress_message(bool enabled,
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

void consider_sync_candidate(const std::vector<int16_t>& pcm,
                             double preamble_pos,
                             double span,
                             SyncLock* best) {
    const double score = sync_score_at(pcm, preamble_pos, span);
    if (score > best->score + 1e-6 ||
        (score > best->score - 0.02 && preamble_pos < best->preamble_pos)) {
        best->preamble_pos = preamble_pos;
        best->sync_pos = preamble_pos + PREAMBLE_SYMBOLS * span;
        best->symbol_span = span;
        best->score = score;
    }
}

double find_energy_onset(const std::vector<int16_t>& pcm) {
    const size_t window = SYMBOL_SAMPLES / 2;
    if (pcm.size() <= window) return 0.0;
    for (size_t i = 0; i + window < pcm.size(); i += 8) {
        double avg_abs = 0.0;
        for (size_t j = 0; j < window; ++j) avg_abs += std::abs(double(pcm[i + j]));
        avg_abs /= double(window);
        if (avg_abs > 1500.0) return double(i);
    }
    return 0.0;
}

}  // namespace

SyncLock::SyncLock()
    : preamble_pos(0.0), sync_pos(0.0), symbol_span(NOMINAL_SPAN), score(-1.0) {}

double sync_score_at(const std::vector<int16_t>& pcm,
                     double preamble_pos,
                     double span) {
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    const double sync_pos = preamble_pos + PREAMBLE_SYMBOLS * span;
    if (preamble_pos < 0.0 ||
        sync_pos + SYNC_SYMBOLS * span >= double(pcm.size())) {
        return -1.0;
    }

    double total = 0.0;
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        total += sample::corr_score(
            pcm, sync_pos + i * span, span, waveform::symbol_template(sync[i], 1));
    }

    // A few preamble checks suppress false locks in arbitrary leading audio.
    const int probes[] = {0, 12, 24, 36};
    for (int idx : probes) {
        total += 0.35 * sample::corr_score(
            pcm, preamble_pos + idx * span, span, waveform::symbol_template(0, 1));
    }
    return total / double(SYNC_SYMBOLS + 4 * 0.35);
}

double preamble_score_at(const std::vector<int16_t>& pcm,
                         double preamble_pos,
                         double span) {
    if (preamble_pos < 0.0 ||
        preamble_pos + PREAMBLE_SYMBOLS * span >= double(pcm.size())) {
        return -1.0;
    }
    double total = 0.0;
    int probes = 0;
    for (int i = 0; i < PREAMBLE_SYMBOLS; i += 4) {
        total += sample::corr_score(
            pcm, preamble_pos + i * span, span, waveform::symbol_template(0, 1));
        ++probes;
    }
    return probes > 0 ? total / double(probes) : -1.0;
}

SyncLock find_sync(const std::vector<int16_t>& pcm,
                   bool verbose,
                   std::clock_t* progress_clock) {
    SyncLock best;
    if (pcm.size() < size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * SYMBOL_SAMPLES)) {
        throw std::runtime_error("PCM too short");
    }

    const double onset = find_energy_onset(pcm);
    progress_message(verbose, progress_clock,
                     "sync acquisition: local search around energy onset sample " +
                         std::to_string(size_t(std::max(0.0, onset))),
                     true);
    for (int scale = 88; scale <= 112; scale += 2) {
        const double span = NOMINAL_SPAN * double(scale) / 100.0;
        for (double pre = onset - 0.75 * span; pre <= onset + 0.75 * span; pre += 2.0) {
            consider_sync_candidate(pcm, pre, span, &best);
        }
    }

    // The energy-onset search is fast and handles normal recordings with
    // leading silence. Fall back to a full sliding scan only if that local lock
    // is weak, which keeps long frames reasonable on Raspberry Pi-class CPUs.
    if (best.score < 0.25) {
        progress_message(verbose, progress_clock,
                         "sync acquisition: local score " + std::to_string(best.score) +
                             " is weak, starting full sliding scan",
                         true);
        const int coarse_step = SYMBOL_SAMPLES / 2;
        for (int scale = 88; scale <= 112; scale += 4) {
            const double span = NOMINAL_SPAN * double(scale) / 100.0;
            const double frame_prefix = (PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * span;
            if (frame_prefix >= double(pcm.size())) continue;
            for (double pre = 0.0; pre + frame_prefix < double(pcm.size());
                 pre += coarse_step) {
                consider_sync_candidate(pcm, pre, span, &best);
                const double denom = std::max(1.0, double(pcm.size()) - frame_prefix);
                const int pct =
                    int(std::min(100.0, std::max(0.0, 100.0 * pre / denom)));
                progress_message(verbose, progress_clock,
                                 "sync acquisition: full scan scale " +
                                     std::to_string(scale) + "%, window " +
                                     std::to_string(pct) + "%, best score " +
                                     std::to_string(best.score));
            }
        }
    }

    progress_message(verbose, progress_clock,
                     "sync acquisition: refining best lock at sample " +
                         std::to_string(size_t(std::max(0.0, best.preamble_pos))) +
                         ", score " + std::to_string(best.score),
                     true);
    SyncLock refined = best;
    for (double span = best.symbol_span - 6.0; span <= best.symbol_span + 6.0; span += 1.0) {
        if (span < NOMINAL_SPAN * 0.85 || span > NOMINAL_SPAN * 1.15) continue;
        for (double pre = best.preamble_pos - SYMBOL_SAMPLES;
             pre <= best.preamble_pos + SYMBOL_SAMPLES; pre += 4.0) {
            consider_sync_candidate(pcm, pre, span, &refined);
        }
    }

    if (refined.score < 0.14) throw std::runtime_error("Sync not found");
    if (verbose) {
        std::cerr << "Sync score=" << refined.score
                  << ", preamble_pos=" << refined.preamble_pos
                  << ", span=" << refined.symbol_span
                  << ", drift=" << ((refined.symbol_span / NOMINAL_SPAN) - 1.0) * 100.0
                  << "%\n";
    }
    return refined;
}

}  // namespace sync
}  // namespace chirp
