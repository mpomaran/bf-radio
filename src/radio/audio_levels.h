#ifndef BF_RADIO_SRC_RADIO_AUDIO_LEVELS_H_
#define BF_RADIO_SRC_RADIO_AUDIO_LEVELS_H_

#include <string>

namespace radio {

void set_playback_level(const std::string& device_id, double level, bool verbose);
void set_capture_level(const std::string& device_id, double level, bool verbose);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_AUDIO_LEVELS_H_
