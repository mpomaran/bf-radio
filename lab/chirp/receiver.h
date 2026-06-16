// receiver.h
//
// Public in-memory chirp receiver API. This is intentionally small: callers
// provide PCM samples plus explicit receiver options and receive payload bytes
// plus diagnostics without spawning the CLI binary.

#ifndef BF_RADIO_LAB_CHIRP_RECEIVER_H_
#define BF_RADIO_LAB_CHIRP_RECEIVER_H_

#include <cstdint>
#include <vector>

#include "lab/chirp/receiver_diagnostics.h"
#include "lab/chirp/receiver_options.h"

namespace chirp {
namespace receiver {

struct DecodeResult {
    bool ok;
    std::vector<uint8_t> payload;
    DecodeAttemptDiagnostics diagnostics;

    DecodeResult() : ok(false), payload(), diagnostics() {}
};

DecodeResult decode_payload_from_pcm(const std::vector<int16_t>& pcm,
                                     const ReceiverOptions& options);

}  // namespace receiver
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_RECEIVER_H_
