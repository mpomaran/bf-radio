#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

/*
  pcm_audio_channel_impair.cpp - deterministic speaker/recorder/microphone-ish
  channel model for raw modem PCM.

  The model is intentionally simple and local:
    silence -> sample-rate offset -> gain/envelope -> DC blocker
    -> optional 300-3000 Hz band-pass -> echo -> drops/fades -> noise
    -> impulsive noise -> clipping

  It is not a calibrated acoustic model. It exists to reproduce the class of
  damage seen in a laptop -> phone recorder -> computer microphone path.
  The checked-in testdata captures show roughly 0.14-0.20 received/sent RMS
  ratio and about 10-11k received peak, so the default model is intentionally
  quieter than the generated modem PCM.
*/

struct Options {
    std::string input;
    std::string output;
    std::string audio_level = "normal";
    int leading_samples = 10280;
    int trailing_samples = 9160;
    double gain = 0.70;
    double time_scale = 1.0;
    bool time_scale_set = false;
    bool time_scale_ppm_set = false;
    double fade = 0.18;
    bool bandpass_enabled = true;
    double bandpass_low_hz = 300.0;
    double bandpass_high_hz = 3000.0;
    double lowpass_alpha = 0.72;
    double highpass_pole = 0.995;
    int echo_delay = 23;
    double echo_gain = 0.16;
    double noise_amplitude = 180.0;
    bool awgn_snr_set = false;
    double awgn_snr_db = 30.0;
    int drop_count = 0;
    int drop_min_ms = 20;
    int drop_max_ms = 200;
    double drop_depth = 0.0;
    double clip_level = 32767.0;
    bool clip_percent_set = false;
    double clip_percent = 0.0;
    double impulse_probability = 0.0;
    double impulse_amplitude = 0.0;
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

static double deterministic_uniform01(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return double((*state >> 8) & 0x00FFFFFFu) / double(0x01000000u);
}

static double deterministic_gaussian(uint32_t* state) {
    const double pi = 3.14159265358979323846;
    const double u1 = std::max(1e-12, deterministic_uniform01(state));
    const double u2 = deterministic_uniform01(state);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * pi * u2);
}

static double sample_at(const std::vector<int16_t>& samples, double pos) {
    if (samples.empty()) return 0.0;
    if (pos <= 0.0) return double(samples.front());
    if (pos >= double(samples.size() - 1)) return double(samples.back());
    const size_t i = size_t(pos);
    const double frac = pos - double(i);
    return double(samples[i]) * (1.0 - frac) + double(samples[i + 1]) * frac;
}

static double audio_level_gain(const std::string& level) {
    if (level == "quiet") return 0.45;
    if (level == "normal") return 1.0;
    if (level == "loud") return 1.55;
    if (level == "overdrive") return 2.25;
    throw std::runtime_error("audio level must be quiet, normal, loud, or overdrive");
}

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double z1 = 0.0;
    double z2 = 0.0;

    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

static Biquad make_biquad_lowpass(double sample_rate, double cutoff_hz) {
    const double pi = 3.14159265358979323846;
    const double q = 0.7071067811865476;
    const double w0 = 2.0 * pi * cutoff_hz / sample_rate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = ((1.0 - c) * 0.5) / a0;
    b.b1 = (1.0 - c) / a0;
    b.b2 = ((1.0 - c) * 0.5) / a0;
    b.a1 = (-2.0 * c) / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

static Biquad make_biquad_highpass(double sample_rate, double cutoff_hz) {
    const double pi = 3.14159265358979323846;
    const double q = 0.7071067811865476;
    const double w0 = 2.0 * pi * cutoff_hz / sample_rate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = ((1.0 + c) * 0.5) / a0;
    b.b1 = -(1.0 + c) / a0;
    b.b2 = ((1.0 + c) * 0.5) / a0;
    b.a1 = (-2.0 * c) / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

static double rms_of(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum2 = 0.0;
    for (double v : values) sum2 += v * v;
    return std::sqrt(sum2 / double(values.size()));
}

static double percentile_abs(std::vector<double> values, double clipped_percent) {
    if (values.empty() || clipped_percent <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    for (double& v : values) v = std::abs(v);
    std::sort(values.begin(), values.end());
    const double keep = std::max(0.0, std::min(100.0, 100.0 - clipped_percent));
    size_t index = size_t((keep / 100.0) * double(values.size() - 1));
    index = std::min(index, values.size() - 1);
    return std::max(1.0, values[index]);
}

static void apply_drops(std::vector<double>& work, const Options& options, uint32_t* rng) {
    if (options.drop_count <= 0) return;
    const int sample_rate = 8000;
    const int min_samples = std::max(1, options.drop_min_ms * sample_rate / 1000);
    const int max_samples = std::max(min_samples, options.drop_max_ms * sample_rate / 1000);
    if (work.empty()) return;

    for (int d = 0; d < options.drop_count; ++d) {
        const double u_pos = deterministic_uniform01(rng);
        const double u_len = deterministic_uniform01(rng);
        const size_t start = size_t(u_pos * double(work.size()));
        const int len = min_samples + int(u_len * double(max_samples - min_samples + 1));
        const size_t end = std::min(work.size(), start + size_t(len));
        for (size_t i = start; i < end; ++i) work[i] *= options.drop_depth;
    }
}

static std::vector<int16_t> apply_channel(const std::vector<int16_t>& in,
                                          const Options& options) {
    if (options.leading_samples < 0 || options.trailing_samples < 0) {
        throw std::runtime_error("Silence lengths must be non-negative");
    }
    if (options.time_scale <= 0.0) throw std::runtime_error("time scale must be positive");
    if (options.lowpass_alpha <= 0.0 || options.lowpass_alpha > 1.0) {
        throw std::runtime_error("lowpass alpha must be in (0, 1]");
    }
    if (options.highpass_pole < 0.0 || options.highpass_pole >= 1.0) {
        throw std::runtime_error("highpass pole must be in [0, 1)");
    }
    if (options.bandpass_low_hz <= 0.0 || options.bandpass_high_hz <= options.bandpass_low_hz ||
        options.bandpass_high_hz >= 4000.0) {
        throw std::runtime_error("band-pass must satisfy 0 < low < high < 4000 Hz");
    }
    if (options.drop_count < 0 || options.drop_min_ms <= 0 ||
        options.drop_max_ms < options.drop_min_ms) {
        throw std::runtime_error("invalid drop settings");
    }
    if (options.drop_depth < 0.0 || options.drop_depth > 1.0) {
        throw std::runtime_error("drop depth must be in [0, 1]");
    }
    if (options.clip_level <= 0.0) throw std::runtime_error("clip level must be positive");
    if (options.clip_percent < 0.0 || options.clip_percent >= 100.0) {
        throw std::runtime_error("clip percent must be in [0, 100)");
    }
    if (options.impulse_probability < 0.0 || options.impulse_probability > 1.0) {
        throw std::runtime_error("impulse probability must be in [0, 1]");
    }

    const size_t leading = size_t(options.leading_samples);
    const size_t trailing = size_t(options.trailing_samples);
    const size_t echo_extra = size_t(std::max(0, options.echo_delay));
    const size_t body_samples =
        std::max<size_t>(1, size_t(std::lround(double(in.size()) * options.time_scale)));
    std::vector<double> work(leading + body_samples + trailing + echo_extra, 0.0);

    double prev_in = 0.0;
    double prev_hp = 0.0;
    double lp = 0.0;
    Biquad bp_hp = make_biquad_highpass(8000.0, options.bandpass_low_hz);
    Biquad bp_lp = make_biquad_lowpass(8000.0, options.bandpass_high_hz);
    const double level_gain = audio_level_gain(options.audio_level);
    for (size_t i = 0; i < body_samples; ++i) {
        const double t = body_samples > 1 ? double(i) / double(body_samples - 1) : 0.0;
        const double envelope =
            options.gain * level_gain * std::max(0.05, 1.0 - options.fade * t);
        const double x = sample_at(in, double(i) / options.time_scale) * envelope;

        const double hp = x - prev_in + options.highpass_pole * prev_hp;
        prev_in = x;
        prev_hp = hp;

        double filtered = hp;
        if (options.bandpass_enabled) {
            filtered = bp_lp.process(bp_hp.process(filtered));
        } else {
            lp += options.lowpass_alpha * (hp - lp);
            filtered = lp;
        }
        const size_t out_pos = leading + i;
        work[out_pos] += filtered;
        if (options.echo_delay > 0) {
            work[out_pos + size_t(options.echo_delay)] += filtered * options.echo_gain;
        }
    }

    uint32_t rng = options.seed;
    apply_drops(work, options, &rng);
    const double signal_rms = rms_of(work);
    const double awgn_rms =
        options.awgn_snr_set ? signal_rms / std::pow(10.0, options.awgn_snr_db / 20.0) : 0.0;
    const double clip_from_percent =
        options.clip_percent_set ? percentile_abs(work, options.clip_percent)
                                 : std::numeric_limits<double>::infinity();
    const double clip_level = std::min(options.clip_level, clip_from_percent);

    std::vector<int16_t> out;
    out.reserve(work.size());
    for (double v : work) {
        v += options.noise_amplitude * deterministic_noise(&rng);
        if (options.awgn_snr_set) v += awgn_rms * deterministic_gaussian(&rng);
        if (options.impulse_probability > 0.0 &&
            deterministic_uniform01(&rng) < options.impulse_probability) {
            v += options.impulse_amplitude * deterministic_noise(&rng);
        }
        v = std::max(-clip_level, std::min(clip_level, v));
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
            "[--audio-level quiet|normal|loud|overdrive] "
            "[--time-scale X | --time-scale-ppm PPM] "
            "[--fade X] [--bandpass 0|1] [--bandpass-low-hz X] "
            "[--bandpass-high-hz X] [--lowpass-alpha X] [--highpass-pole X] "
            "[--echo-delay N] [--echo-gain X] [--noise-amplitude X] "
            "[--snr-db X] [--drop-count N] [--drop-min-ms N] [--drop-max-ms N] "
            "[--drop-depth X] [--clip-level X] [--clip-percent X] "
            "[--impulse-probability X] [--impulse-amplitude X] [--seed N]");
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
        } else if (arg == "--audio-level") {
            options.audio_level = require_value(arg);
        } else if (arg == "--gain") {
            options.gain = parse_double(require_value(arg), arg);
        } else if (arg == "--time-scale") {
            options.time_scale = parse_double(require_value(arg), arg);
            options.time_scale_set = true;
        } else if (arg == "--time-scale-ppm") {
            const double ppm = parse_double(require_value(arg), arg);
            options.time_scale = 1.0 + ppm / 1000000.0;
            options.time_scale_ppm_set = true;
        } else if (arg == "--fade") {
            options.fade = parse_double(require_value(arg), arg);
        } else if (arg == "--bandpass") {
            options.bandpass_enabled = parse_int(require_value(arg), arg) != 0;
        } else if (arg == "--bandpass-low-hz") {
            options.bandpass_low_hz = parse_double(require_value(arg), arg);
        } else if (arg == "--bandpass-high-hz") {
            options.bandpass_high_hz = parse_double(require_value(arg), arg);
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
        } else if (arg == "--snr-db") {
            options.awgn_snr_db = parse_double(require_value(arg), arg);
            options.awgn_snr_set = true;
        } else if (arg == "--drop-count") {
            options.drop_count = parse_int(require_value(arg), arg);
        } else if (arg == "--drop-min-ms") {
            options.drop_min_ms = parse_int(require_value(arg), arg);
        } else if (arg == "--drop-max-ms") {
            options.drop_max_ms = parse_int(require_value(arg), arg);
        } else if (arg == "--drop-depth") {
            options.drop_depth = parse_double(require_value(arg), arg);
        } else if (arg == "--clip-level") {
            options.clip_level = parse_double(require_value(arg), arg);
        } else if (arg == "--clip-percent") {
            options.clip_percent = parse_double(require_value(arg), arg);
            options.clip_percent_set = true;
        } else if (arg == "--impulse-probability") {
            options.impulse_probability = parse_double(require_value(arg), arg);
        } else if (arg == "--impulse-amplitude") {
            options.impulse_amplitude = parse_double(require_value(arg), arg);
        } else if (arg == "--seed") {
            options.seed = uint32_t(parse_int(require_value(arg), arg));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    if (options.time_scale_set && options.time_scale_ppm_set) {
        throw std::runtime_error("Use only one of --time-scale or --time-scale-ppm");
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
