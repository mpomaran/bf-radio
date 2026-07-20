#ifndef BF_RADIO_SRC_RADIO_AUDIO_IO_H_
#define BF_RADIO_SRC_RADIO_AUDIO_IO_H_

#include <string>

#include "src/radio/wav.h"

namespace radio {

void play_audio(const AudioBuffer& audio, const std::string& device_id);
void record_audio_to_segments(const std::string& device_id,
                              const std::string& output_prefix,
                              uint32_t sample_rate,
                              uint16_t channels,
                              uint32_t segment_seconds,
                              double gain,
                              uint32_t duration_seconds,
                              bool verbose);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_AUDIO_IO_H_
