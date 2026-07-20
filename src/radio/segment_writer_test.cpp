#include "src/radio/segment_writer.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

std::filesystem::path tmp_dir() {
    const char* env = std::getenv("TEST_TMPDIR");
    return env ? std::filesystem::path(env) : std::filesystem::temp_directory_path();
}

std::vector<int16_t> read_samples(const std::filesystem::path& path) {
    return radio::read_wav_as_pcm16(path.string()).samples;
}

}  // namespace

int main() {
    const auto prefix = (tmp_dir() / "segment_exact").string();
    std::vector<int16_t> input;
    for (int i = 0; i < 53; ++i) {
        input.push_back(int16_t((i * 997) - 26000));
    }

    radio::SegmentWriter writer(prefix, 10, 1, 2);  // 20 samples per segment.
    writer.append(input.data(), 7);
    writer.append(input.data() + 7, 19);
    writer.append(input.data() + 26, input.size() - 26);
    writer.close();

    assert(writer.total_samples_written() == input.size());
    assert(writer.segments_written() == 3);

    std::vector<int16_t> recovered;
    for (int i = 0; i < 3; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "segment_exact_%06d.wav", i);
        const auto part = read_samples(tmp_dir() / name);
        recovered.insert(recovered.end(), part.begin(), part.end());
    }
    assert(recovered == input);
    assert(read_samples(tmp_dir() / "segment_exact_000000.wav").size() == 20);
    assert(read_samples(tmp_dir() / "segment_exact_000001.wav").size() == 20);
    assert(read_samples(tmp_dir() / "segment_exact_000002.wav").size() == 13);
    return 0;
}
