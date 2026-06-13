#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

/*
  pcm_audio_channel_impair.cpp - deterministic speaker/recorder/microphone-ish
  channel model for raw modem PCM.

  The model is intentionally simple and local:
    silence -> gain/envelope -> DC blocker -> one-pole low-pass -> echo -> noise

  It is not a calibrated acoustic model. It exists to reproduce the class of
  damage seen in a laptop -> phone recorder -> computer microphone path.
*/

struct Options {
    std::string input;
    std::string output;
    int leading_samples = 10280;
    int trailing_samples = 9160;
    double gain = 0.70;
    double fade = 0.18;
    double lowpass_alpha = 0.72;
    double highpass_pole = 0.995;
    int echo_delay = 23;
    double echo_gain = 0.16;
    double noise_amplitude = 180.0;
    uint32_t seed = 0xC001C0DEu;
};

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open input file");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open output file");
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

static std::vector<int16_t> bytes_to_samples(std::vector<uint8_t> bytes) {
    if (bytes.size() % 2) bytes.pop_back();
    std::vector<int16_t> samples;
    samples.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2) {
        samples.push_back(int16_t(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8)));
    }
    return samples;
}

static std::vector<uint8_t> samples_to_bytes(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (int16_t sample : samples) {
        const uint16_t u = uint16_t(sample);
        bytes.push_back(uint8_t(u & 0xFF));
        bytes.push_back(uint8_t((u >> 8) & 0xFF));
    }
    return bytes;
}

static int parse_int(const std::string& value, const std::string& name) {
    size_t parsed = 0;
    int result = 0;
    try {
        result = std::stoi(value, &parsed, 0);
    } catch (...) {
        throw std::runtime_error("Invalid integer for " + name + ": " + value);
    }
    if (parsed != value.size()) throw std::runtime_error("Invalid integer for " + name);
    return result;
}

static double parse_double(const std::string& value, const std::string& name) {
    size_t parsed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &parsed);
    } catch (...) {
        throw std::runtime_error("Invalid number for " + name + ": " + value);
    }
    if (parsed != value.size()) throw std::runtime_error("Invalid number for " + name);
    return result;
}

static double deterministic_noise(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    const double u = double((*state >> 8) & 0x00FFFFFFu) / double(0x01000000u);
    return 2.0 * u - 1.0;
}

static std::vector<int16_t> apply_channel(const std::vector<int16_t>& in,
                                          const Options& options) {
    if (options.leading_samples < 0 || options.trailing_samples < 0) {
        throw std::runtime_error("Silence lengths must be non-negative");
    }
    if (options.lowpass_alpha <= 0.0 || options.lowpass_alpha > 1.0) {
        throw std::runtime_error("lowpass alpha must be in (0, 1]");
    }
    if (options.highpass_pole < 0.0 || options.highpass_pole >= 1.0) {
        throw std::runtime_error("highpass pole must be in [0, 1)");
    }

    const size_t leading = size_t(options.leading_samples);
    const size_t trailing = size_t(options.trailing_samples);
    const size_t echo_extra = size_t(std::max(0, options.echo_delay));
    std::vector<double> work(leading + in.size() + trailing + echo_extra, 0.0);

    double prev_in = 0.0;
    double prev_hp = 0.0;
    double lp = 0.0;
    for (size_t i = 0; i < in.size(); ++i) {
        const double t = in.size() > 1 ? double(i) / double(in.size() - 1) : 0.0;
        const double envelope = options.gain * std::max(0.05, 1.0 - options.fade * t);
        const double x = double(in[i]) * envelope;

        const double hp = x - prev_in + options.highpass_pole * prev_hp;
        prev_in = x;
        prev_hp = hp;

        lp += options.lowpass_alpha * (hp - lp);
        const size_t out_pos = leading + i;
        work[out_pos] += lp;
        if (options.echo_delay > 0) {
            work[out_pos + size_t(options.echo_delay)] += lp * options.echo_gain;
        }
    }

    uint32_t rng = options.seed;
    std::vector<int16_t> out;
    out.reserve(work.size());
    for (double v : work) {
        v += options.noise_amplitude * deterministic_noise(&rng);
        v = std::max(-32768.0, std::min(32767.0, v));
        out.push_back(int16_t(std::lround(v)));
    }
    return out;
}

static Options parse_args(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(
            "Usage: pcm_audio_channel_impair input.pcm output.pcm "
            "[--leading-samples N] [--trailing-samples N] [--gain X] "
            "[--fade X] [--lowpass-alpha X] [--highpass-pole X] "
            "[--echo-delay N] [--echo-gain X] [--noise-amplitude X] [--seed N]");
    }

    Options options;
    options.input = argv[1];
    options.output = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };

        if (arg == "--leading-samples") {
            options.leading_samples = parse_int(require_value(arg), arg);
        } else if (arg == "--trailing-samples") {
            options.trailing_samples = parse_int(require_value(arg), arg);
        } else if (arg == "--gain") {
            options.gain = parse_double(require_value(arg), arg);
        } else if (arg == "--fade") {
            options.fade = parse_double(require_value(arg), arg);
        } else if (arg == "--lowpass-alpha") {
            options.lowpass_alpha = parse_double(require_value(arg), arg);
        } else if (arg == "--highpass-pole") {
            options.highpass_pole = parse_double(require_value(arg), arg);
        } else if (arg == "--echo-delay") {
            options.echo_delay = parse_int(require_value(arg), arg);
        } else if (arg == "--echo-gain") {
            options.echo_gain = parse_double(require_value(arg), arg);
        } else if (arg == "--noise-amplitude") {
            options.noise_amplitude = parse_double(require_value(arg), arg);
        } else if (arg == "--seed") {
            options.seed = uint32_t(parse_int(require_value(arg), arg));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return options;
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        const auto samples = bytes_to_samples(read_file(options.input));
        const auto impaired = apply_channel(samples, options);
        write_file(options.output, samples_to_bytes(impaired));
        std::cerr << "Impaired " << samples.size() << " samples into "
                  << impaired.size() << " samples\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
