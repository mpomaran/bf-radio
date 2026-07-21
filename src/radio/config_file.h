#ifndef BF_RADIO_SRC_RADIO_CONFIG_FILE_H_
#define BF_RADIO_SRC_RADIO_CONFIG_FILE_H_

#include <string>

namespace radio {

inline constexpr double kDefaultTxOutputLevel = 0.4;
inline constexpr double kDefaultRxInputLevel = 0.4;

struct ToolConfig {
    std::string serial_port;
    std::string playback_device;
    std::string recording_device;
    double tx_output_level = kDefaultTxOutputLevel;
    double rx_input_level = kDefaultRxInputLevel;
    bool ptt_active_low = false;
};

ToolConfig read_tool_config(const std::string& path);
void write_tool_config(const std::string& path, const ToolConfig& cfg);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_CONFIG_FILE_H_
