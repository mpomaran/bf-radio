// sync_acquisition.h
//
// Preamble/sync search for chirp frames. This module owns acquisition scoring
// and lock refinement, but not payload demodulation.

#ifndef BF_RADIO_LAB_CHIRP_SYNC_ACQUISITION_H_
#define BF_RADIO_LAB_CHIRP_SYNC_ACQUISITION_H_

#include <cstdint>
#include <ctime>
#include <vector>

namespace chirp {
namespace sync {

struct SyncLock {
    double preamble_pos;
    double sync_pos;
    double symbol_span;
    double score;

    SyncLock();
};

double sync_score_at(const std::vector<int16_t>& pcm,
                     double preamble_pos,
                     double span);

double preamble_score_at(const std::vector<int16_t>& pcm,
                         double preamble_pos,
                         double span);

SyncLock find_sync(const std::vector<int16_t>& pcm,
                   bool verbose = true,
                   std::clock_t* progress_clock = nullptr);

}  // namespace sync
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_SYNC_ACQUISITION_H_
