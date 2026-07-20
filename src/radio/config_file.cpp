#include "src/radio/config_file.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace radio {
namespace {

std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

ToolConfig read_tool_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config file: " + path);
    ToolConfig cfg;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "serial") cfg.serial_port = value;
        else if (key == "playback") cfg.playback_device = value;
        else if (key == "recording") cfg.recording_device = value;
        else if (key == "tx_volume") cfg.tx_volume = std::stod(value);
        else if (key == "rx_gain") cfg.rx_gain = std::stod(value);
    }
    return cfg;
}

void write_tool_config(const std::string& path, const ToolConfig& cfg) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write config file: " + path);
    f << "# bf-radio Digirig profile\n"
      << "serial=" << cfg.serial_port << "\n"
      << "playback=" << cfg.playback_device << "\n"
      << "recording=" << cfg.recording_device << "\n"
      << "tx_volume=" << cfg.tx_volume << "\n"
      << "rx_gain=" << cfg.rx_gain << "\n";
}

}  // namespace radio
