#include "src/radio/wav.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

std::filesystem::path tmp_path(const char* name) {
    const char* env = std::getenv("TEST_TMPDIR");
    std::filesystem::path dir = env ? env : std::filesystem::temp_directory_path();
    return dir / name;
}

}  // namespace

int main() {
    radio::AudioBuffer input;
    input.sample_rate = 8000;
    input.channels = 1;
    input.samples = {-32768, -20000, -1, 0, 1, 12345, 32767};

    const auto path = tmp_path("bf_radio_wav_test.wav");
    radio::write_wav_pcm16(path.string(), input);
    const auto output = radio::read_wav_as_pcm16(path.string());
    assert(output.sample_rate == input.sample_rate);
    assert(output.channels == input.channels);
    assert(output.samples == input.samples);

    radio::AudioBuffer stereo;
    stereo.sample_rate = 16000;
    stereo.channels = 2;
    stereo.samples = {1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000};
    const auto converted = radio::convert_audio(stereo, 8000, 1, 0.5);
    assert(converted.sample_rate == 8000);
    assert(converted.channels == 1);
    assert(!converted.samples.empty());

    std::filesystem::remove(path);
    return 0;
}
