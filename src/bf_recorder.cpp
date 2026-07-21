#include "src/radio/audio_io.h"
#include "src/radio/config_file.h"
#include "src/radio/wav.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0
        << " [--config PATH] [--audio-device DEVICE] [--segment-seconds N] [--rate Hz] [--channels N]"
           " [--input-level X]"
           " [--duration-seconds N]"
           " [--verbose] [--no-auto-level]"
           " output_prefix\n"
        << "\n"
        << "Continuously records Digirig audio into consecutive WAV files.\n"
        << "Defaults: 48000 Hz, mono, signed 16-bit PCM, 30-second files,\n"
        << "input-level 0.4.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string audio_device;
        std::string config_path;
        std::string output_prefix;
        double input_level = radio::kDefaultRxInputLevel;
        bool input_level_set = false;
        uint32_t segment_seconds = 30;
        uint32_t duration_seconds = 0;
        uint32_t rate = radio::kDefaultSampleRate;
        uint16_t channels = radio::kDefaultChannels;
        bool verbose = false;
        bool auto_input_level = true;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else if (a == "--audio-device" && i + 1 < argc) {
                audio_device = argv[++i];
            } else if (a == "--config" && i + 1 < argc) {
                config_path = argv[++i];
            } else if (a == "--segment-seconds" && i + 1 < argc) {
                segment_seconds = uint32_t(std::stoul(argv[++i]));
            } else if (a == "--duration-seconds" && i + 1 < argc) {
                duration_seconds = uint32_t(std::stoul(argv[++i]));
            } else if (a == "--verbose") {
                verbose = true;
            } else if (a == "--no-auto-level") {
                auto_input_level = false;
            } else if (a == "--rate" && i + 1 < argc) {
                rate = uint32_t(std::stoul(argv[++i]));
            } else if (a == "--channels" && i + 1 < argc) {
                channels = uint16_t(std::stoul(argv[++i]));
            } else if (a == "--input-level" && i + 1 < argc) {
                input_level = std::stod(argv[++i]);
                input_level_set = true;
            } else if (output_prefix.empty()) {
                output_prefix = a;
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        if (!config_path.empty()) {
            const auto cfg = radio::read_tool_config(config_path);
            if (audio_device.empty()) audio_device = cfg.recording_device;
            if (!input_level_set) input_level = cfg.rx_input_level;
        }
        if (output_prefix.empty()) {
            usage(argv[0]);
            return 1;
        }
        std::cerr << "Recording " << rate << " Hz, " << channels << " channel(s), PCM16; "
                  << segment_seconds << " s per file; input level " << input_level
                  << (auto_input_level ? "; auto-level on" : "; auto-level off") << "\n";
        radio::record_audio_to_segments(audio_device, output_prefix, rate, channels,
                                        segment_seconds, input_level, auto_input_level,
                                        duration_seconds, verbose);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
