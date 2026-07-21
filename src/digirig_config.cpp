#include "src/radio/devices.h"
#include "src/radio/config_file.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " [--serial PORT] [--playback DEVICE] [--recording DEVICE]\n"
        << "      [--tx-output-level X] [--rx-input-level X]\n"
        << "      [--ptt-active-low|--ptt-active-high] [--write-config PATH] [--yes]\n"
        << "\n"
        << "Lists connected serial/audio devices and chooses likely Digirig 1.11 endpoints.\n"
        << "Default TX volume and RX input level are 0.4, intended as about 40%.\n"
        << "Use selected values with bf_wav_tx/bf_recorder. Device IDs are platform-specific.\n";
}

void print_list(const char* title, const std::vector<radio::DeviceCandidate>& devices) {
    std::cout << title << ":\n";
    if (devices.empty()) {
        std::cout << "  (none found)\n";
        return;
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] id=" << devices[i].id << " name=" << devices[i].name
                  << (devices[i].likely_digirig ? " likely-digirig" : "") << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string serial_req;
        std::string playback_req;
        std::string recording_req;
        std::string write_config;
        double tx_output_level = radio::kDefaultTxOutputLevel;
        double rx_input_level = radio::kDefaultRxInputLevel;
        bool ptt_active_low = false;
        bool interactive = true;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else if (a == "--yes") {
                interactive = false;
            } else if (a == "--serial" && i + 1 < argc) {
                serial_req = argv[++i];
            } else if (a == "--playback" && i + 1 < argc) {
                playback_req = argv[++i];
            } else if (a == "--recording" && i + 1 < argc) {
                recording_req = argv[++i];
            } else if (a == "--tx-output-level" && i + 1 < argc) {
                tx_output_level = std::stod(argv[++i]);
            } else if (a == "--rx-input-level" && i + 1 < argc) {
                rx_input_level = std::stod(argv[++i]);
            } else if (a == "--ptt-active-low") {
                ptt_active_low = true;
            } else if (a == "--ptt-active-high") {
                ptt_active_low = false;
            } else if (a == "--write-config" && i + 1 < argc) {
                write_config = argv[++i];
            } else {
                usage(argv[0]);
                return 1;
            }
        }

        const auto serial = radio::list_serial_devices();
        const auto playback = radio::list_playback_devices();
        const auto recording = radio::list_recording_devices();
        print_list("Serial/PTT devices", serial);
        print_list("Playback devices", playback);
        print_list("Recording devices", recording);

        const int s = radio::choose_candidate(serial, serial_req, interactive);
        const int p = radio::choose_candidate(playback, playback_req, interactive);
        const int r = radio::choose_candidate(recording, recording_req, interactive);

        std::cout << "\nSelected configuration:\n";
        if (s >= 0) std::cout << "  serial=" << serial[size_t(s)].id << "\n";
        if (p >= 0) std::cout << "  playback=" << playback[size_t(p)].id << "\n";
        if (r >= 0) std::cout << "  recording=" << recording[size_t(r)].id << "\n";
        std::cout << "  tx_output_level=" << tx_output_level << "\n"
                  << "  rx_input_level=" << rx_input_level << "\n"
                  << "  ptt_active_low=" << (ptt_active_low ? "true" : "false") << "\n";
        if (!write_config.empty()) {
            radio::ToolConfig cfg;
            if (s >= 0) cfg.serial_port = serial[size_t(s)].id;
            if (p >= 0) cfg.playback_device = playback[size_t(p)].id;
            if (r >= 0) cfg.recording_device = recording[size_t(r)].id;
            cfg.tx_output_level = tx_output_level;
            cfg.rx_input_level = rx_input_level;
            cfg.ptt_active_low = ptt_active_low;
            radio::write_tool_config(write_config, cfg);
            std::cout << "  wrote_config=" << write_config << "\n";
        }
        std::cout << "\nExamples:\n"
                  << "  bazel run //src:bf_wav_tx -- --ptt "
                  << (s >= 0 ? serial[size_t(s)].id : "PORT")
                  << " --audio-device " << (p >= 0 ? playback[size_t(p)].id : "DEVICE")
                  << " input.wav\n"
                  << "  bazel run //src:bf_recorder -- --audio-device "
                  << (r >= 0 ? recording[size_t(r)].id : "DEVICE") << " out/rec\n"
                  << "  bazel run //src:bf_wav_tx -- --config bf-radio.conf input.wav\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
