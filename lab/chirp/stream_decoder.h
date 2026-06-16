// stream_decoder.h
//
// Streaming acquisition result types. The scanner may consume inspected PCM,
// request more samples, reject a bad candidate, or return a decoded frame.

#ifndef BF_RADIO_LAB_CHIRP_STREAM_DECODER_H_
#define BF_RADIO_LAB_CHIRP_STREAM_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chirp {
namespace stream {

enum class StreamScanStatus {
    NoFrameWindowConsumed,
    NeedMoreSamples,
    FrameDecoded,
    InvalidFrameRejected
};

const char* stream_scan_status_name(StreamScanStatus status);

struct StreamScanResult {
    StreamScanStatus status;

    /*
      Number of samples from the front of this PCM window that a streaming
      caller may erase. If this is zero, keep the whole window and append more
      samples before scanning again.

      frame_start_sample and frame_end_sample are offsets inside the supplied
      window. frame_end_sample is exclusive and marks the end of the decoded
      chirp data symbols, not necessarily trailing silence after the frame.
    */
    size_t discard_prefix_samples;
    size_t frame_start_sample;
    size_t frame_end_sample;
    double acquisition_score;
    double estimated_symbol_span;
    std::vector<uint8_t> payload;

    StreamScanResult();
};

size_t clamp_discard(size_t value, size_t pcm_size);

}  // namespace stream
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_STREAM_DECODER_H_
