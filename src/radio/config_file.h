#ifndef BF_RADIO_SRC_RADIO_CONFIG_FILE_H_
#define BF_RADIO_SRC_RADIO_CONFIG_FILE_H_

#include <string>

namespace radio {

inline constexpr double kDefaultTxVolume = 0.4;
inline constexpr double kDefaultRxGain = 0.4;

struct ToolConfig {
    std::string serial_port;
    std::string playback_device;
    std::string recording_device;
    double tx_volume = kDefaultTxVolume;
    double rx_gain = kDefaultRxGain;
};

ToolConfig read_tool_config(const std::string& path);
void write_tool_config(const std::string& path, const ToolConfig& cfg);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_CONFIG_FILE_H_
