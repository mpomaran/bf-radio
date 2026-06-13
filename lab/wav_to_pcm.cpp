#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

/*
  wav_to_pcm.cpp - Convert WAV audio to raw PCM for the chirp/P4 modems.

  Output format:
    - 8000 Hz by default
    - mono
    - signed 16-bit little-endian
    - no WAV header

  Supported input:
    - RIFF/WAVE, little-endian
    - PCM integer: 8, 16, 24, or 32 bits
    - IEEE float: 32 or 64 bits
    - any channel count >= 1

  Usage:
    ./wav_to_pcm input.wav output.pcm [target_sample_rate]
*/

struct WavFormat {
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
};

static const uint16_t WAVE_FORMAT_PCM = 1;
static const uint16_t WAVE_FORMAT_IEEE_FLOAT = 3;
static const uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open input WAV file");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open output PCM file");
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

static uint16_t u16le(const std::vector<uint8_t>& b, size_t pos) {
    if (pos + 2 > b.size()) throw std::runtime_error("Unexpected end of WAV file");
    return uint16_t(b[pos]) | (uint16_t(b[pos + 1]) << 8);
}

static uint32_t u32le(const std::vector<uint8_t>& b, size_t pos) {
    if (pos + 4 > b.size()) throw std::runtime_error("Unexpected end of WAV file");
    return uint32_t(b[pos]) |
           (uint32_t(b[pos + 1]) << 8) |
           (uint32_t(b[pos + 2]) << 16) |
           (uint32_t(b[pos + 3]) << 24);
}

static bool tag_eq(const std::vector<uint8_t>& b, size_t pos, const char tag[4]) {
    return pos + 4 <= b.size() &&
           b[pos] == uint8_t(tag[0]) &&
           b[pos + 1] == uint8_t(tag[1]) &&
           b[pos + 2] == uint8_t(tag[2]) &&
           b[pos + 3] == uint8_t(tag[3]);
}

static bool wav_extensible_subformat_matches(const std::vector<uint8_t>& b,
                                             size_t pos,
                                             uint16_t format_tag) {
    static const uint8_t guid_tail[12] = {
        0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00,
        0xAA, 0x00, 0x38, 0x9B, 0x71
    };
    if (pos + 16 > b.size()) return false;
    if (u16le(b, pos) != format_tag || b[pos + 2] != 0 || b[pos + 3] != 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(guid_tail); ++i) {
        if (b[pos + 4 + i] != guid_tail[i]) return false;
    }
    return true;
}

static int32_t read_signed_le(const uint8_t* p, uint16_t bits) {
    if (bits == 8) {
        return int32_t(int(p[0]) - 128);
    }
    if (bits == 16) {
        return int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
    }
    if (bits == 24) {
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (v & 0x00800000) v |= int32_t(0xFF000000);
        return v;
    }
    if (bits == 32) {
        return int32_t(uint32_t(p[0]) |
                       (uint32_t(p[1]) << 8) |
                       (uint32_t(p[2]) << 16) |
                       (uint32_t(p[3]) << 24));
    }
    throw std::runtime_error("Unsupported PCM bit depth");
}

static double read_float_le(const uint8_t* p, uint16_t bits) {
    if (bits == 32) {
        uint32_t u = uint32_t(p[0]) |
                     (uint32_t(p[1]) << 8) |
                     (uint32_t(p[2]) << 16) |
                     (uint32_t(p[3]) << 24);
        float v = 0.0f;
        static_assert(sizeof(v) == sizeof(u), "float must be 32-bit");
        std::memcpy(&v, &u, sizeof(v));
        return double(v);
    }
    if (bits == 64) {
        uint64_t u = uint64_t(p[0]) |
                     (uint64_t(p[1]) << 8) |
                     (uint64_t(p[2]) << 16) |
                     (uint64_t(p[3]) << 24) |
                     (uint64_t(p[4]) << 32) |
                     (uint64_t(p[5]) << 40) |
                     (uint64_t(p[6]) << 48) |
                     (uint64_t(p[7]) << 56);
        double v = 0.0;
        static_assert(sizeof(v) == sizeof(u), "double must be 64-bit");
        std::memcpy(&v, &u, sizeof(v));
        return v;
    }
    throw std::runtime_error("Unsupported IEEE float bit depth");
}

static double pcm_scale(uint16_t bits) {
    if (bits == 8) return 128.0;
    if (bits == 16) return 32768.0;
    if (bits == 24) return 8388608.0;
    if (bits == 32) return 2147483648.0;
    throw std::runtime_error("Unsupported PCM bit depth");
}

static std::vector<double> decode_wav_to_mono(const std::vector<uint8_t>& wav,
                                              uint32_t* sample_rate) {
    if (wav.size() < 12 || !tag_eq(wav, 0, "RIFF") || !tag_eq(wav, 8, "WAVE")) {
        throw std::runtime_error("Input is not a little-endian RIFF/WAVE file");
    }

    WavFormat fmt;
    size_t data_pos = 0;
    size_t data_size = 0;

    for (size_t pos = 12; pos + 8 <= wav.size();) {
        const uint32_t chunk_size = u32le(wav, pos + 4);
        const size_t chunk_data = pos + 8;
        const size_t next = chunk_data + chunk_size + (chunk_size & 1U);
        if (chunk_data + chunk_size > wav.size()) {
            throw std::runtime_error("WAV chunk extends past end of file");
        }

        if (tag_eq(wav, pos, "fmt ")) {
            if (chunk_size < 16) throw std::runtime_error("WAV fmt chunk is too short");
            fmt.audio_format = u16le(wav, chunk_data);
            fmt.channels = u16le(wav, chunk_data + 2);
            fmt.sample_rate = u32le(wav, chunk_data + 4);
            fmt.block_align = u16le(wav, chunk_data + 12);
            fmt.bits_per_sample = u16le(wav, chunk_data + 14);
            if (fmt.audio_format == WAVE_FORMAT_EXTENSIBLE) {
                if (chunk_size < 40) {
                    throw std::runtime_error("WAVE_FORMAT_EXTENSIBLE fmt chunk is too short");
                }
                const size_t subformat_pos = chunk_data + 24;
                if (wav_extensible_subformat_matches(wav, subformat_pos, WAVE_FORMAT_PCM)) {
                    fmt.audio_format = WAVE_FORMAT_PCM;
                } else if (wav_extensible_subformat_matches(wav, subformat_pos,
                                                           WAVE_FORMAT_IEEE_FLOAT)) {
                    fmt.audio_format = WAVE_FORMAT_IEEE_FLOAT;
                } else {
                    throw std::runtime_error("Unsupported WAVE_FORMAT_EXTENSIBLE subformat");
                }
            }
        } else if (tag_eq(wav, pos, "data")) {
            data_pos = chunk_data;
            data_size = chunk_size;
        }
        pos = next;
    }

    if (fmt.audio_format == 0) throw std::runtime_error("Missing WAV fmt chunk");
    if (data_size == 0) throw std::runtime_error("Missing or empty WAV data chunk");
    if (fmt.channels == 0) throw std::runtime_error("WAV channel count is zero");
    if (fmt.sample_rate == 0) throw std::runtime_error("WAV sample rate is zero");

    const uint16_t bytes_per_sample = uint16_t((fmt.bits_per_sample + 7) / 8);
    const uint16_t expected_align = uint16_t(fmt.channels * bytes_per_sample);
    if (fmt.block_align < expected_align || expected_align == 0) {
        throw std::runtime_error("Unsupported WAV block alignment");
    }

    if (fmt.audio_format == WAVE_FORMAT_PCM) {
        if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 ||
              fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32)) {
            throw std::runtime_error("Unsupported PCM bit depth");
        }
    } else if (fmt.audio_format == WAVE_FORMAT_IEEE_FLOAT) {
        if (!(fmt.bits_per_sample == 32 || fmt.bits_per_sample == 64)) {
            throw std::runtime_error("Unsupported IEEE float bit depth");
        }
    } else {
        throw std::runtime_error("Unsupported WAV format; only PCM and IEEE float are supported");
    }

    const size_t frames = data_size / fmt.block_align;
    std::vector<double> mono;
    mono.reserve(frames);

    for (size_t frame = 0; frame < frames; ++frame) {
        const uint8_t* frame_ptr = wav.data() + data_pos + frame * fmt.block_align;
        double sum = 0.0;
        for (uint16_t ch = 0; ch < fmt.channels; ++ch) {
            const uint8_t* sample_ptr = frame_ptr + size_t(ch) * bytes_per_sample;
            double sample = 0.0;
            if (fmt.audio_format == WAVE_FORMAT_PCM) {
                sample = double(read_signed_le(sample_ptr, fmt.bits_per_sample)) /
                         pcm_scale(fmt.bits_per_sample);
            } else {
                sample = read_float_le(sample_ptr, fmt.bits_per_sample);
            }
            sum += std::max(-1.0, std::min(1.0, sample));
        }
        mono.push_back(sum / double(fmt.channels));
    }

    *sample_rate = fmt.sample_rate;
    return mono;
}

static std::vector<double> resample_linear(const std::vector<double>& in,
                                           uint32_t src_rate,
                                           uint32_t dst_rate) {
    if (in.empty() || src_rate == dst_rate) return in;
    const size_t out_len = std::max<size_t>(
        1, size_t(std::llround(double(in.size()) * double(dst_rate) / double(src_rate))));
    std::vector<double> out;
    out.reserve(out_len);

    for (size_t i = 0; i < out_len; ++i) {
        const double src_pos = double(i) * double(src_rate) / double(dst_rate);
        const size_t i0 = std::min<size_t>(size_t(src_pos), in.size() - 1);
        const size_t i1 = std::min<size_t>(i0 + 1, in.size() - 1);
        const double frac = src_pos - double(i0);
        out.push_back(in[i0] + (in[i1] - in[i0]) * frac);
    }
    return out;
}

static std::vector<uint8_t> encode_pcm16le(const std::vector<double>& samples) {
    std::vector<uint8_t> out;
    out.reserve(samples.size() * 2);
    for (double s : samples) {
        s = std::max(-1.0, std::min(1.0, s));
        int v = int(std::lround(s * 32768.0));
        v = std::max(-32768, std::min(32767, v));
        const uint16_t u = uint16_t(int16_t(v));
        out.push_back(uint8_t(u & 0xFF));
        out.push_back(uint8_t((u >> 8) & 0xFF));
    }
    return out;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) {
            std::cerr
                << "Usage:\n"
                << "  " << argv[0] << " input.wav output.pcm [target_sample_rate]\n"
                << "\n"
                << "Output defaults:\n"
                << "  sample_rate: 8000 Hz\n"
                << "  channels: 1 mono\n"
                << "  sample_type: signed 16-bit little-endian raw PCM\n";
            return 1;
        }

        const std::string in_path = argv[1];
        const std::string out_path = argv[2];
        uint32_t target_rate = 8000;
        if (argc == 4) {
            target_rate = uint32_t(std::stoul(argv[3]));
            if (target_rate == 0) throw std::runtime_error("Target sample rate must be positive");
        }

        uint32_t source_rate = 0;
        const auto wav = read_file(in_path);
        const auto mono = decode_wav_to_mono(wav, &source_rate);
        const auto converted = resample_linear(mono, source_rate, target_rate);
        const auto pcm = encode_pcm16le(converted);
        write_file(out_path, pcm);

        std::cerr << "Converted WAV -> raw PCM16 mono\n"
                  << "Input samples: " << mono.size() << " at " << source_rate << " Hz\n"
                  << "Output samples: " << converted.size() << " at " << target_rate << " Hz\n"
                  << "Output bytes: " << pcm.size() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
