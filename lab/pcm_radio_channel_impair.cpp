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
  pcm_radio_channel_impair.cpp - deterministic raw PCM channel impairment tool.

  This is a compact regression model for modem experiments. It is not a
  calibrated RF/acoustic simulator. The goal is to exercise the receiver against
  common channel damage classes with reproducible output:

    leading/trailing idle
    slight time-scale error and sinusoidal timing wander
    slow amplitude fading
    DC blocking and one-pole low-pass bandwidth limiting
    two delayed multipath/echo taps
    deterministic white and impulsive noise
    optional clipping
*/

struct Options {
    std::string input;
    std::string output;
    int leading_samples = 1600;
    int trailing_samples = 1600;
    double gain = 0.78;
    double time_scale = 1.0;
    double timing_wander_samples = 0.20;
    int timing_wander_period = 32000;
    double fade_depth = 0.12;
    int fade_period = 48000;
    double lowpass_alpha = 0.82;
    double highpass_pole = 0.996;
    int echo1_delay = 19;
    double echo1_gain = 0.10;
    int echo2_delay = 73;
    double echo2_gain = 0.035;
    double noise_amplitude = 95.0;
    int impulse_interval = 4096;
    double impulse_amplitude = 900.0;
    double clip_level = 30000.0;
    double dc_offset = 0.0;
    uint32_t seed = 0x52616469u;
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

static double sample_at(const std::vector<int16_t>& samples, double pos) {
    if (samples.empty()) return 0.0;
    if (pos <= 0.0) return double(samples.front());
    if (pos >= double(samples.size() - 1)) return double(samples.back());
    const size_t i = size_t(pos);
    const double frac = pos - double(i);
    return double(samples[i]) * (1.0 - frac) + double(samples[i + 1]) * frac;
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

static void validate_options(const Options& options) {
    if (options.leading_samples < 0 || options.trailing_samples < 0) {
        throw std::runtime_error("Silence lengths must be non-negative");
    }
    if (options.time_scale <= 0.0) throw std::runtime_error("time scale must be positive");
    if (options.timing_wander_period <= 0) {
        throw std::runtime_error("timing wander period must be positive");
    }
    if (options.fade_period <= 0) throw std::runtime_error("fade period must be positive");
    if (options.lowpass_alpha <= 0.0 || options.lowpass_alpha > 1.0) {
        throw std::runtime_error("lowpass alpha must be in (0, 1]");
    }
    if (options.highpass_pole < 0.0 || options.highpass_pole >= 1.0) {
        throw std::runtime_error("highpass pole must be in [0, 1)");
    }
    if (options.echo1_delay < 0 || options.echo2_delay < 0) {
        throw std::runtime_error("echo delays must be non-negative");
    }
    if (options.impulse_interval < 0) {
        throw std::runtime_error("impulse interval must be non-negative");
    }
    if (options.clip_level <= 0.0) throw std::runtime_error("clip level must be positive");
}

static std::vector<int16_t> apply_channel(const std::vector<int16_t>& in,
                                          const Options& options) {
    validate_options(options);
    const double pi = 3.14159265358979323846;
    const size_t leading = size_t(options.leading_samples);
    const size_t trailing = size_t(options.trailing_samples);
    const size_t body_samples =
        std::max<size_t>(1, size_t(std::lround(double(in.size()) * options.time_scale)));
    const size_t echo_extra =
        size_t(std::max(options.echo1_delay, options.echo2_delay));

    std::vector<double> work(leading + body_samples + trailing + echo_extra, 0.0);

    double prev_in = 0.0;
    double prev_hp = 0.0;
    double lp = 0.0;
    for (size_t i = 0; i < body_samples; ++i) {
        const double wander =
            options.timing_wander_samples *
            std::sin(2.0 * pi * double(i) / double(options.timing_wander_period));
        const double src_pos = double(i) / options.time_scale + wander;
        double x = sample_at(in, src_pos);

        const double fade =
            1.0 - options.fade_depth * 0.5 *
                      (1.0 - std::cos(2.0 * pi * double(i) / double(options.fade_period)));
        x = x * options.gain * std::max(0.0, fade) + options.dc_offset;

        const double hp = x - prev_in + options.highpass_pole * prev_hp;
        prev_in = x;
        prev_hp = hp;
        lp += options.lowpass_alpha * (hp - lp);

        const size_t out_pos = leading + i;
        work[out_pos] += lp;
        if (options.echo1_delay > 0) {
            work[out_pos + size_t(options.echo1_delay)] += lp * options.echo1_gain;
        }
        if (options.echo2_delay > 0) {
            work[out_pos + size_t(options.echo2_delay)] += lp * options.echo2_gain;
        }
    }

    uint32_t rng = options.seed;
    std::vector<int16_t> out;
    out.reserve(work.size());
    for (size_t i = 0; i < work.size(); ++i) {
        double v = work[i] + options.noise_amplitude * deterministic_noise(&rng);
        if (options.impulse_interval > 0 && (i % size_t(options.impulse_interval)) == 0) {
            v += options.impulse_amplitude * deterministic_noise(&rng);
        }
        v = std::max(-options.clip_level, std::min(options.clip_level, v));
        v = std::max(-32768.0, std::min(32767.0, v));
        out.push_back(int16_t(std::lround(v)));
    }
    return out;
}

static Options parse_args(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(
            "Usage: pcm_radio_channel_impair input.pcm output.pcm "
            "[--leading-samples N] [--trailing-samples N] [--gain X] "
            "[--time-scale X] [--timing-wander-samples X] "
            "[--timing-wander-period N] [--fade-depth X] [--fade-period N] "
            "[--lowpass-alpha X] [--highpass-pole X] "
            "[--echo1-delay N] [--echo1-gain X] "
            "[--echo2-delay N] [--echo2-gain X] "
            "[--noise-amplitude X] [--impulse-interval N] "
            "[--impulse-amplitude X] [--clip-level X] [--dc-offset X] [--seed N]");
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
        } else if (arg == "--time-scale") {
            options.time_scale = parse_double(require_value(arg), arg);
        } else if (arg == "--timing-wander-samples") {
            options.timing_wander_samples = parse_double(require_value(arg), arg);
        } else if (arg == "--timing-wander-period") {
            options.timing_wander_period = parse_int(require_value(arg), arg);
        } else if (arg == "--fade-depth") {
            options.fade_depth = parse_double(require_value(arg), arg);
        } else if (arg == "--fade-period") {
            options.fade_period = parse_int(require_value(arg), arg);
        } else if (arg == "--lowpass-alpha") {
            options.lowpass_alpha = parse_double(require_value(arg), arg);
        } else if (arg == "--highpass-pole") {
            options.highpass_pole = parse_double(require_value(arg), arg);
        } else if (arg == "--echo1-delay") {
            options.echo1_delay = parse_int(require_value(arg), arg);
        } else if (arg == "--echo1-gain") {
            options.echo1_gain = parse_double(require_value(arg), arg);
        } else if (arg == "--echo2-delay") {
            options.echo2_delay = parse_int(require_value(arg), arg);
        } else if (arg == "--echo2-gain") {
            options.echo2_gain = parse_double(require_value(arg), arg);
        } else if (arg == "--noise-amplitude") {
            options.noise_amplitude = parse_double(require_value(arg), arg);
        } else if (arg == "--impulse-interval") {
            options.impulse_interval = parse_int(require_value(arg), arg);
        } else if (arg == "--impulse-amplitude") {
            options.impulse_amplitude = parse_double(require_value(arg), arg);
        } else if (arg == "--clip-level") {
            options.clip_level = parse_double(require_value(arg), arg);
        } else if (arg == "--dc-offset") {
            options.dc_offset = parse_double(require_value(arg), arg);
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
        std::cerr << "Radio-impaired " << samples.size() << " samples into "
                  << impaired.size() << " samples\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
