#include "lab/chirp/stream_decoder.h"

#include <algorithm>

namespace chirp {
namespace stream {

const char* stream_scan_status_name(StreamScanStatus status) {
    switch (status) {
        case StreamScanStatus::NoFrameWindowConsumed: return "NoFrameWindowConsumed";
        case StreamScanStatus::NeedMoreSamples: return "NeedMoreSamples";
        case StreamScanStatus::FrameDecoded: return "FrameDecoded";
        case StreamScanStatus::InvalidFrameRejected: return "InvalidFrameRejected";
    }
    return "Unknown";
}

StreamScanResult::StreamScanResult()
    : status(StreamScanStatus::NoFrameWindowConsumed),
      discard_prefix_samples(0), frame_start_sample(0), frame_end_sample(0),
      acquisition_score(0.0), estimated_symbol_span(0.0), payload() {}

size_t clamp_discard(size_t value, size_t pcm_size) {
    return std::min(value, pcm_size);
}

}  // namespace stream
}  // namespace chirp
