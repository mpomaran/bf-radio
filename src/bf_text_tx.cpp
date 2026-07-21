#include "src/radio/audio_io.h"
#include "src/radio/audio_levels.h"
#include "src/radio/config_file.h"
#include "src/radio/ptt.h"
#include "src/radio/wav.h"

#include "lab/chirp/config.h"
#include "lab/chirp/file_io.h"
#include "lab/chirp/modulator.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
radio::RtsPtt* g_active_ptt = nullptr;

BOOL WINAPI tx_ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_SHUTDOWN_EVENT) {
        if (g_active_ptt) g_active_ptt->force_off();
        return FALSE;
    }
    return FALSE;
}
#endif

void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0
        << " [--config PATH] [--ptt PORT] [--audio-device DEVICE]"
           " [--output-level X] [--verbose]"
           " [--ptt-active-low|--ptt-active-high]"
           " [--pre-ptt-ms N] [--post-ptt-ms N]"
           " [--save-wav PATH] input.txt\n"
        << "\n"
        << "Encodes input.txt with the lab chirp modem and transmits it with Digirig PTT.\n";
}

void release_ptt(radio::RtsPtt* ptt, bool verbose) {
    const bool accepted =
        ptt->force_off_until(std::chrono::milliseconds(3000), std::chrono::milliseconds(50));
    if (verbose) {
        std::cerr << "PTT release "
                  << (accepted ? "accepted by serial driver" : "not confirmed by serial driver")
                  << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string ptt_port;
        std::string audio_device;
        std::string config_path;
        std::string input;
        std::string save_wav;
        double output_level = radio::kDefaultTxOutputLevel;
        bool output_level_set = false;
        bool ptt_active_low = false;
        bool ptt_polarity_set = false;
        bool verbose = false;
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
            } else if (a == "--output-level" && i + 1 < argc) {
                output_level = std::stod(argv[++i]);
                output_level_set = true;
            } else if (a == "--verbose") {
                verbose = true;
            } else if (a == "--ptt-active-low") {
                ptt_active_low = true;
                ptt_polarity_set = true;
            } else if (a == "--ptt-active-high") {
                ptt_active_low = false;
                ptt_polarity_set = true;
            } else if (a == "--pre-ptt-ms" && i + 1 < argc) {
                pre_ms = std::stoi(argv[++i]);
            } else if (a == "--post-ptt-ms" && i + 1 < argc) {
                post_ms = std::stoi(argv[++i]);
            } else if (a == "--save-wav" && i + 1 < argc) {
                save_wav = argv[++i];
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
            if (!output_level_set) output_level = cfg.tx_output_level;
            if (!ptt_polarity_set) ptt_active_low = cfg.ptt_active_low;
        }
        if (input.empty()) {
            usage(argv[0]);
            return 1;
        }

        const std::vector<uint8_t> payload = chirp::io::read_file(input);
        radio::AudioBuffer chirp_audio;
        chirp_audio.sample_rate = chirp::config::SAMPLE_RATE;
        chirp_audio.channels = 1;
        chirp_audio.samples = chirp::modulator::encode_payload_to_pcm(payload);
        const auto tx =
            radio::convert_audio(chirp_audio, radio::kDefaultSampleRate, radio::kDefaultChannels, 1.0);

        std::cerr << "Payload: " << payload.size() << " byte(s)\n"
                  << "Chirp audio: " << radio::describe_audio(chirp_audio) << "\n"
                  << "Transmit: " << radio::describe_audio(tx)
                  << "; output level " << output_level
                  << "; PTT RTS " << (ptt_active_low ? "active-low" : "active-high")
                  << "\n";

        if (!save_wav.empty()) {
            radio::write_wav_pcm16(save_wav, chirp_audio);
            std::cerr << "Saved chirp WAV: " << save_wav << "\n";
        }
        if (ptt_port.empty()) {
            if (!save_wav.empty()) return 0;
            throw std::runtime_error("PTT port is required unless --save-wav is used");
        }

        radio::set_playback_level(audio_device, output_level, verbose);
        radio::RtsPtt ptt(ptt_port, ptt_active_low);
#ifdef _WIN32
        g_active_ptt = &ptt;
        SetConsoleCtrlHandler(tx_ctrl_handler, TRUE);
#endif
        try {
            ptt.set(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(pre_ms));
            radio::play_audio(tx, audio_device);
            std::this_thread::sleep_for(std::chrono::milliseconds(post_ms));
            release_ptt(&ptt, verbose);
        } catch (...) {
            release_ptt(&ptt, verbose);
#ifdef _WIN32
            g_active_ptt = nullptr;
            SetConsoleCtrlHandler(tx_ctrl_handler, FALSE);
#endif
            throw;
        }
#ifdef _WIN32
        g_active_ptt = nullptr;
        SetConsoleCtrlHandler(tx_ctrl_handler, FALSE);
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
