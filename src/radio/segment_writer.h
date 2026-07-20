#ifndef BF_RADIO_SRC_RADIO_SEGMENT_WRITER_H_
#define BF_RADIO_SRC_RADIO_SEGMENT_WRITER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/radio/wav.h"

namespace radio {

class SegmentWriter {
public:
    SegmentWriter(std::string output_prefix,
                  uint32_t sample_rate,
                  uint16_t channels,
                  uint32_t segment_seconds,
                  bool verbose = false);
    void append(const int16_t* samples, size_t count);
    void append(const std::vector<int16_t>& samples) { append(samples.data(), samples.size()); }
    void close();
    uint64_t total_samples_written() const { return total_samples_written_; }
    uint64_t segments_written() const { return segment_index_; }

private:
    std::string segment_path(uint64_t index) const;
    void announce_current_segment() const;
    void flush_segment(bool final_segment);
    size_t samples_per_segment_ = 0;
    std::string output_prefix_;
    AudioBuffer current_;
    bool verbose_ = false;
    uint64_t segment_index_ = 0;
    uint64_t total_samples_written_ = 0;
};

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_SEGMENT_WRITER_H_
