#ifndef BF_RADIO_SRC_RADIO_WAV_H_
#define BF_RADIO_SRC_RADIO_WAV_H_

#include <cstdint>
#include <string>
#include <vector>

namespace radio {

inline constexpr uint32_t kLabSampleRate = 8000;
inline constexpr uint16_t kLabChannels = 1;
inline constexpr uint16_t kLabBitsPerSample = 16;

struct AudioBuffer {
    uint32_t sample_rate = kLabSampleRate;
    uint16_t channels = kLabChannels;
    std::vector<int16_t> samples;  // Interleaved PCM16 little-endian logical samples.
};

AudioBuffer read_wav_as_pcm16(const std::string& path);
void write_wav_pcm16(const std::string& path, const AudioBuffer& audio);
AudioBuffer convert_audio(const AudioBuffer& in,
                          uint32_t sample_rate,
                          uint16_t channels,
                          double volume);
std::string describe_audio(const AudioBuffer& audio);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_WAV_H_
