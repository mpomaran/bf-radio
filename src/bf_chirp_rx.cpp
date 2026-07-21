#include "src/radio/audio_io.h"
#include "src/radio/config_file.h"
#include "src/radio/wav.h"

#include "lab/chirp/config.h"
#include "lab/chirp/file_io.h"
#include "lab/chirp/receiver.h"
#include "lab/chirp/receiver_options.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0
        << " [--config PATH] [--audio-device DEVICE] [--segment-seconds N]"
           " [--record-seconds N] [--input-level X] [--verbose] [--no-auto-level]"
           " [--decode-only] [--output PATH] output_prefix\n"
        << "\n"
        << "Records Digirig audio into short WAV segments, stitches them, and decodes\n"
        << "the lab chirp modem payload. Use --decode-only to demodulate existing segments.\n";
}

std::string segment_path(const std::string& prefix, uint64_t index) {
    std::ostringstream ss;
    ss << prefix << "_" << std::setw(6) << std::setfill('0') << index << ".wav";
    return ss.str();
}

radio::AudioBuffer read_stitched_segments(const std::string& prefix, bool verbose) {
    radio::AudioBuffer stitched;
    bool initialized = false;
    uint64_t files = 0;
    for (uint64_t i = 0;; ++i) {
        const std::string path = segment_path(prefix, i);
        if (!std::filesystem::exists(path)) break;
        const auto part = radio::read_wav_as_pcm16(path);
        if (!initialized) {
            stitched.sample_rate = part.sample_rate;
            stitched.channels = part.channels;
            initialized = true;
        }
        if (part.sample_rate != stitched.sample_rate || part.channels != stitched.channels) {
            throw std::runtime_error("segment format changed: " + path);
        }
        stitched.samples.insert(stitched.samples.end(), part.samples.begin(), part.samples.end());
        ++files;
        if (verbose) std::cerr << "Read segment " << path << ": " << radio::describe_audio(part) << "\n";
    }
    if (files == 0) throw std::runtime_error("no segment WAV files found for prefix: " + prefix);
    if (verbose) {
        std::cerr << "Stitched " << files << " segment(s): "
                  << radio::describe_audio(stitched) << "\n";
    }
    return stitched;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string audio_device;
        std::string config_path;
        std::string output_prefix;
        std::string output_path;
        double input_level = radio::kDefaultRxInputLevel;
        bool input_level_set = false;
        uint32_t segment_seconds = 3;
        uint32_t record_seconds = 30;
        bool verbose = false;
        bool auto_input_level = true;
        bool decode_only = false;

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
            } else if (a == "--record-seconds" && i + 1 < argc) {
                record_seconds = uint32_t(std::stoul(argv[++i]));
            } else if (a == "--input-level" && i + 1 < argc) {
                input_level = std::stod(argv[++i]);
                input_level_set = true;
            } else if (a == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (a == "--verbose") {
                verbose = true;
            } else if (a == "--no-auto-level") {
                auto_input_level = false;
            } else if (a == "--decode-only") {
                decode_only = true;
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

        if (!decode_only) {
            std::cerr << "Recording chirp RX: " << record_seconds << " s, "
                      << segment_seconds << " s segment(s), input level " << input_level
                      << (auto_input_level ? "; auto-level on" : "; auto-level off") << "\n";
            radio::record_audio_to_segments(audio_device, output_prefix, radio::kDefaultSampleRate,
                                            radio::kDefaultChannels, segment_seconds, input_level,
                                            auto_input_level, record_seconds, verbose);
        }

        const auto stitched = read_stitched_segments(output_prefix, verbose);
        const auto modem_audio =
            radio::convert_audio(stitched, chirp::config::SAMPLE_RATE, 1, 1.0);
        if (verbose) {
            std::cerr << "Demod input: " << radio::describe_audio(modem_audio) << "\n";
        }

        chirp::receiver::ReceiverOptions options;
        chirp::receiver::apply_receiver_profile_defaults(
            &options, chirp::receiver::ReceiverProfile::Robust);
        options.rx_diagnostics_enabled = verbose;

        const auto result = chirp::receiver::decode_payload_from_pcm(modem_audio.samples, options);
        if (!result.ok) {
            throw std::runtime_error("chirp decode failed");
        }
        if (!output_path.empty()) {
            chirp::io::write_file(output_path, result.payload);
            std::cerr << "Decoded " << result.payload.size() << " byte(s) -> " << output_path << "\n";
        } else {
            std::cout.write(reinterpret_cast<const char*>(result.payload.data()),
                            std::streamsize(result.payload.size()));
            std::cout << "\n";
            std::cerr << "Decoded " << result.payload.size() << " byte(s) OK\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
