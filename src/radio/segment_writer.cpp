#include "src/radio/segment_writer.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace radio {

SegmentWriter::SegmentWriter(std::string output_prefix,
                             uint32_t sample_rate,
                             uint16_t channels,
                             uint32_t segment_seconds,
                             bool verbose)
    : output_prefix_(std::move(output_prefix)), verbose_(verbose) {
    if (sample_rate == 0 || channels == 0 || segment_seconds == 0) {
        throw std::runtime_error("bad segment writer parameters");
    }
    current_.sample_rate = sample_rate;
    current_.channels = channels;
    samples_per_segment_ = size_t(sample_rate) * channels * segment_seconds;
    announce_current_segment();
}

void SegmentWriter::append(const int16_t* samples, size_t count) {
    size_t pos = 0;
    while (pos < count) {
        const size_t room = samples_per_segment_ - current_.samples.size();
        const size_t n = std::min(room, count - pos);
        current_.samples.insert(current_.samples.end(), samples + pos, samples + pos + n);
        pos += n;
        total_samples_written_ += n;
        if (current_.samples.size() == samples_per_segment_) flush_segment(false);
    }
}

void SegmentWriter::close() {
    flush_segment(true);
}

std::string SegmentWriter::segment_path(uint64_t index) const {
    std::ostringstream path;
    path << output_prefix_ << "_" << std::setw(6) << std::setfill('0') << index << ".wav";
    return path.str();
}

void SegmentWriter::announce_current_segment() const {
    if (!verbose_) return;
    std::cerr << "Recording segment " << segment_index_ << " -> "
              << segment_path(segment_index_) << "\n";
}

void SegmentWriter::flush_segment(bool final_segment) {
    if (current_.samples.empty()) return;
    if (!final_segment && current_.samples.size() != samples_per_segment_) return;
    const std::string path = segment_path(segment_index_);
    const size_t samples = current_.samples.size();
    write_wav_pcm16(path, current_);
    if (verbose_) {
        std::cerr << "Wrote segment " << segment_index_ << " -> " << path << " ("
                  << samples << " sample(s)" << (final_segment ? ", final" : "") << ")\n";
    }
    ++segment_index_;
    current_.samples.clear();
    if (!final_segment) announce_current_segment();
}

}  // namespace radio
