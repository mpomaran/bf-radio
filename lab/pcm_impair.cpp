#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Options {
    std::string input;
    std::string output;
    std::string region = "all";
    int scale_percent = 100;
    double time_scale = 1.0;
    bool scale_percent_set = false;
    bool time_scale_set = false;
    bool time_scale_ppm_set = false;
    int region_percent = 25;
    int bitflip_stride = 0;
    uint8_t bitflip_mask = 0x01;
};

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open input file");
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>());
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open output file");
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

static int16_t read_i16le(const std::vector<uint8_t>& bytes, size_t sample) {
    const size_t i = sample * 2;
    return int16_t(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8));
}

static void append_i16le(std::vector<uint8_t>& bytes, int16_t sample) {
    bytes.push_back(uint8_t(uint16_t(sample) & 0xFF));
    bytes.push_back(uint8_t((uint16_t(sample) >> 8) & 0xFF));
}

static std::vector<int16_t> bytes_to_samples(const std::vector<uint8_t>& bytes) {
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("PCM input must contain whole 16-bit samples");
    }

    std::vector<int16_t> samples;
    samples.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size() / 2; ++i) {
        samples.push_back(read_i16le(bytes, i));
    }
    return samples;
}

static std::vector<uint8_t> samples_to_bytes(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (int16_t sample : samples) append_i16le(bytes, sample);
    return bytes;
}

static int16_t interpolate(const std::vector<int16_t>& in, double pos) {
    if (in.empty()) return 0;
    if (pos <= 0.0) return in.front();
    if (pos >= double(in.size() - 1)) return in.back();

    const size_t i = size_t(pos);
    const double frac = pos - double(i);
    const double a = double(in[i]);
    const double b = double(in[i + 1]);
    const int value = int(std::lround(a + (b - a) * frac));
    return int16_t(std::clamp(value, -32768, 32767));
}

static std::vector<int16_t> resample_linear(const std::vector<int16_t>& in,
                                            int scale_percent) {
    if (in.empty() || scale_percent == 100) return in;
    if (scale_percent <= 0) throw std::runtime_error("Scale percent must be positive");

    const size_t out_len = std::max<size_t>(
        1, (in.size() * size_t(scale_percent) + 50) / 100);
    std::vector<int16_t> out;
    out.reserve(out_len);

    if (out_len == 1) {
        out.push_back(in.front());
        return out;
    }

    const double step = double(in.size() - 1) / double(out_len - 1);
    for (size_t i = 0; i < out_len; ++i) {
        out.push_back(interpolate(in, double(i) * step));
    }
    return out;
}

static std::vector<int16_t> resample_linear(const std::vector<int16_t>& in,
                                            double time_scale) {
    if (in.empty() || time_scale == 1.0) return in;
    if (time_scale <= 0.0) throw std::runtime_error("Time scale must be positive");

    const size_t out_len = std::max<size_t>(
        1, size_t(std::llround(double(in.size()) * time_scale)));
    std::vector<int16_t> out;
    out.reserve(out_len);

    if (out_len == 1) {
        out.push_back(in.front());
        return out;
    }

    const double step = double(in.size() - 1) / double(out_len - 1);
    for (size_t i = 0; i < out_len; ++i) {
        out.push_back(interpolate(in, double(i) * step));
    }
    return out;
}

static std::pair<size_t, size_t> region_bounds(size_t sample_count,
                                               const std::string& region,
                                               int region_percent) {
    if (region == "all") return {0, sample_count};
    if (region_percent <= 0 || region_percent > 100) {
        throw std::runtime_error("Region percent must be in 1..100");
    }

    const size_t len = std::max<size_t>(1, sample_count * size_t(region_percent) / 100);
    if (region == "start") return {0, std::min(sample_count, len)};
    if (region == "end") return {sample_count - std::min(sample_count, len), sample_count};
    if (region == "middle") {
        const size_t begin = (sample_count - std::min(sample_count, len)) / 2;
        return {begin, begin + std::min(sample_count, len)};
    }

    throw std::runtime_error("Region must be one of: all, start, middle, end");
}

static std::vector<int16_t> impair_timing(const std::vector<int16_t>& in,
                                          const Options& options) {
    const auto [begin, end] = region_bounds(in.size(), options.region, options.region_percent);
    std::vector<int16_t> out;
    out.reserve(in.size());

    out.insert(out.end(), in.begin(), in.begin() + std::ptrdiff_t(begin));
    std::vector<int16_t> segment(in.begin() + std::ptrdiff_t(begin),
                                 in.begin() + std::ptrdiff_t(end));
    const std::vector<int16_t> scaled =
        options.scale_percent_set
            ? resample_linear(segment, options.scale_percent)
            : resample_linear(segment, options.time_scale);
    out.insert(out.end(), scaled.begin(), scaled.end());
    out.insert(out.end(), in.begin() + std::ptrdiff_t(end), in.end());
    return out;
}

static void apply_bit_flips(std::vector<uint8_t>& bytes, int stride, uint8_t mask) {
    if (stride <= 0) return;
    for (size_t i = size_t(stride - 1); i < bytes.size(); i += size_t(stride)) {
        bytes[i] ^= mask;
    }
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

static Options parse_args(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(
            "Usage: pcm_impair input.pcm output.pcm [--region all|start|middle|end] "
            "[--scale-percent N | --time-scale X | --time-scale-ppm PPM] "
            "[--region-percent N] [--bitflip-stride N] [--bitflip-mask N]");
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

        if (arg == "--region") {
            options.region = require_value(arg);
        } else if (arg == "--scale-percent") {
            options.scale_percent = parse_int(require_value(arg), arg);
            options.scale_percent_set = true;
        } else if (arg == "--time-scale") {
            options.time_scale = parse_double(require_value(arg), arg);
            options.time_scale_set = true;
        } else if (arg == "--time-scale-ppm") {
            const double ppm = parse_double(require_value(arg), arg);
            options.time_scale = 1.0 + ppm / 1000000.0;
            options.time_scale_ppm_set = true;
        } else if (arg == "--region-percent") {
            options.region_percent = parse_int(require_value(arg), arg);
        } else if (arg == "--bitflip-stride") {
            options.bitflip_stride = parse_int(require_value(arg), arg);
        } else if (arg == "--bitflip-mask") {
            options.bitflip_mask = uint8_t(parse_int(require_value(arg), arg) & 0xFF);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    const int scale_option_count = (options.scale_percent_set ? 1 : 0) +
                                   (options.time_scale_set ? 1 : 0) +
                                   (options.time_scale_ppm_set ? 1 : 0);
    if (scale_option_count > 1) {
        throw std::runtime_error(
            "Use only one of --scale-percent, --time-scale, or --time-scale-ppm");
    }

    return options;
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        auto bytes = read_file(options.input);
        auto samples = bytes_to_samples(bytes);
        auto impaired_samples = impair_timing(samples, options);
        auto impaired_bytes = samples_to_bytes(impaired_samples);
        apply_bit_flips(impaired_bytes, options.bitflip_stride, options.bitflip_mask);
        write_file(options.output, impaired_bytes);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
