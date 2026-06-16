#include "lab/chirp/stream_decoder.h"

#include <cassert>
#include <string>

int main() {
    const chirp::stream::StreamScanResult result;
    assert(result.status == chirp::stream::StreamScanStatus::NoFrameWindowConsumed);
    assert(result.discard_prefix_samples == 0);
    assert(result.frame_start_sample == 0);
    assert(result.frame_end_sample == 0);
    assert(result.acquisition_score == 0.0);
    assert(result.estimated_symbol_span == 0.0);
    assert(result.payload.empty());

    assert(std::string(chirp::stream::stream_scan_status_name(
               chirp::stream::StreamScanStatus::NoFrameWindowConsumed)) ==
           "NoFrameWindowConsumed");
    assert(std::string(chirp::stream::stream_scan_status_name(
               chirp::stream::StreamScanStatus::NeedMoreSamples)) ==
           "NeedMoreSamples");
    assert(std::string(chirp::stream::stream_scan_status_name(
               chirp::stream::StreamScanStatus::FrameDecoded)) ==
           "FrameDecoded");
    assert(std::string(chirp::stream::stream_scan_status_name(
               chirp::stream::StreamScanStatus::InvalidFrameRejected)) ==
           "InvalidFrameRejected");

    assert(chirp::stream::clamp_discard(5, 10) == 5);
    assert(chirp::stream::clamp_discard(15, 10) == 10);
    return 0;
}
