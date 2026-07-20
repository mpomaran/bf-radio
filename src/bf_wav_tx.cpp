#include "src/radio/audio_io.h"
#include "src/radio/config_file.h"
#include "src/radio/ptt.h"
#include "src/radio/wav.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0
        << " [--config PATH] --ptt PORT [--audio-device DEVICE] [--rate Hz] [--channels N] [--volume X]"
           " [--pre-ptt-ms N] [--post-ptt-ms N] input.wav\n"
        << "\n"
        << "Defaults match lab audio: 8000 Hz, mono, signed 16-bit PCM, volume 0.4.\n"
        << "PTT is asserted with Digirig RTS while the converted WAV is played.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string ptt_port;
        std::string audio_device;
        std::string config_path;
        std::string input;
        uint32_t rate = radio::kLabSampleRate;
        uint16_t channels = radio::kLabChannels;
        double volume = radio::kDefaultTxVolume;
        bool volume_set = false;
        int pre_ms = 150;
        int post_ms = 150;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else if (a == "--ptt" && i + 1 < argc) {
                ptt_port = argv[++i];
            } else if (a == "--config" && i + 1 < argc) {
                config_path = argv[++i];
            } else if (a == "--audio-device" && i + 1 < argc) {
                audio_device = argv[++i];
            } else if (a == "--rate" && i + 1 < argc) {
                rate = uint32_t(std::stoul(argv[++i]));
            } else if (a == "--channels" && i + 1 < argc) {
                channels = uint16_t(std::stoul(argv[++i]));
            } else if (a == "--volume" && i + 1 < argc) {
                volume = std::stod(argv[++i]);
                volume_set = true;
            } else if (a == "--pre-ptt-ms" && i + 1 < argc) {
                pre_ms = std::stoi(argv[++i]);
            } else if (a == "--post-ptt-ms" && i + 1 < argc) {
                post_ms = std::stoi(argv[++i]);
            } else if (input.empty()) {
                input = a;
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        if (!config_path.empty()) {
            const auto cfg = radio::read_tool_config(config_path);
            if (ptt_port.empty()) ptt_port = cfg.serial_port;
            if (audio_device.empty()) audio_device = cfg.playback_device;
            if (!volume_set) volume = cfg.tx_volume;
        }
        if (ptt_port.empty() || input.empty()) {
            usage(argv[0]);
            return 1;
        }

        const auto wav = radio::read_wav_as_pcm16(input);
        const auto tx = radio::convert_audio(wav, rate, channels, volume);
        std::cerr << "Input: " << radio::describe_audio(wav) << "\n"
                  << "Transmit: " << radio::describe_audio(tx) << "\n";

        radio::RtsPtt ptt(ptt_port);
        ptt.set(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(pre_ms));
        try {
            radio::play_audio(tx, audio_device);
        } catch (...) {
            ptt.set(false);
            throw;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(post_ms));
        ptt.set(false);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
