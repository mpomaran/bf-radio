#include "src/radio/wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace radio {
namespace {

constexpr uint16_t kWavePcm = 1;
constexpr uint16_t kWaveFloat = 3;
constexpr uint16_t kWaveExtensible = 0xFFFE;

struct WavFormat {
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
};

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open input WAV: " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

uint16_t u16le(const std::vector<uint8_t>& b, size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("truncated WAV");
    return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}

uint32_t u32le(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("truncated WAV");
    return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) |
           (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
}

bool tag_eq(const std::vector<uint8_t>& b, size_t p, const char tag[4]) {
    return p + 4 <= b.size() && b[p] == uint8_t(tag[0]) &&
           b[p + 1] == uint8_t(tag[1]) && b[p + 2] == uint8_t(tag[2]) &&
           b[p + 3] == uint8_t(tag[3]);
}

bool extensible_matches(const std::vector<uint8_t>& b, size_t p, uint16_t tag) {
    static const uint8_t tail[12] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                     0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    if (p + 16 > b.size()) return false;
    if (u16le(b, p) != tag || b[p + 2] != 0 || b[p + 3] != 0) return false;
    for (size_t i = 0; i < sizeof(tail); ++i) {
        if (b[p + 4 + i] != tail[i]) return false;
    }
    return true;
}

int32_t read_signed_le(const uint8_t* p, uint16_t bits) {
    if (bits == 8) return int32_t(int(p[0]) - 128);
    if (bits == 16) return int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
    if (bits == 24) {
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (v & 0x00800000) v |= int32_t(0xFF000000);
        return v;
    }
    if (bits == 32) {
        return int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                       (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
    }
    throw std::runtime_error("unsupported PCM bit depth");
}

double pcm_scale(uint16_t bits) {
    if (bits == 8) return 128.0;
    if (bits == 16) return 32768.0;
    if (bits == 24) return 8388608.0;
    if (bits == 32) return 2147483648.0;
    throw std::runtime_error("unsupported PCM bit depth");
}

double read_float_le(const uint8_t* p, uint16_t bits) {
    if (bits == 32) {
        uint32_t u = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                     (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        float v = 0.0f;
        std::memcpy(&v, &u, sizeof(v));
        return double(v);
    }
    if (bits == 64) {
        uint64_t u = uint64_t(p[0]) | (uint64_t(p[1]) << 8) |
                     (uint64_t(p[2]) << 16) | (uint64_t(p[3]) << 24) |
                     (uint64_t(p[4]) << 32) | (uint64_t(p[5]) << 40) |
                     (uint64_t(p[6]) << 48) | (uint64_t(p[7]) << 56);
        double v = 0.0;
        std::memcpy(&v, &u, sizeof(v));
        return v;
    }
    throw std::runtime_error("unsupported float bit depth");
}

int16_t clamp_i16(double s) {
    s = std::max(-1.0, std::min(1.0, s));
    int v = int(std::lround(s * 32768.0));
    v = std::max(-32768, std::min(32767, v));
    return int16_t(v);
}

void put_u16(std::ofstream& f, uint16_t v) {
    char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
    f.write(b, 2);
}

void put_u32(std::ofstream& f, uint32_t v) {
    char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF),
                 char((v >> 24) & 0xFF)};
    f.write(b, 4);
}

}  // namespace

AudioBuffer read_wav_as_pcm16(const std::string& path) {
    const auto wav = read_file(path);
    if (wav.size() < 12 || !tag_eq(wav, 0, "RIFF") || !tag_eq(wav, 8, "WAVE")) {
        throw std::runtime_error("input is not RIFF/WAVE");
    }

    WavFormat fmt;
    size_t data_pos = 0;
    size_t data_size = 0;

    for (size_t pos = 12; pos + 8 <= wav.size();) {
        const uint32_t chunk_size = u32le(wav, pos + 4);
        const size_t chunk_data = pos + 8;
        const size_t next = chunk_data + chunk_size + (chunk_size & 1U);
        if (chunk_data + chunk_size > wav.size()) throw std::runtime_error("bad WAV chunk");
        if (tag_eq(wav, pos, "fmt ")) {
            if (chunk_size < 16) throw std::runtime_error("short WAV fmt chunk");
            fmt.audio_format = u16le(wav, chunk_data);
            fmt.channels = u16le(wav, chunk_data + 2);
            fmt.sample_rate = u32le(wav, chunk_data + 4);
            fmt.block_align = u16le(wav, chunk_data + 12);
            fmt.bits_per_sample = u16le(wav, chunk_data + 14);
            if (fmt.audio_format == kWaveExtensible) {
                if (chunk_size < 40) throw std::runtime_error("short extensible WAV fmt");
                const size_t sub = chunk_data + 24;
                if (extensible_matches(wav, sub, kWavePcm)) {
                    fmt.audio_format = kWavePcm;
                } else if (extensible_matches(wav, sub, kWaveFloat)) {
                    fmt.audio_format = kWaveFloat;
                } else {
                    throw std::runtime_error("unsupported extensible WAV format");
                }
            }
        } else if (tag_eq(wav, pos, "data")) {
            data_pos = chunk_data;
            data_size = chunk_size;
        }
        pos = next;
    }

    if (fmt.audio_format == 0) throw std::runtime_error("WAV fmt chunk missing");
    if (data_size == 0) throw std::runtime_error("WAV data chunk missing");
    if (fmt.channels == 0 || fmt.sample_rate == 0) throw std::runtime_error("bad WAV format");
    const uint16_t bytes = uint16_t((fmt.bits_per_sample + 7) / 8);
    if (fmt.block_align < uint16_t(fmt.channels * bytes)) {
        throw std::runtime_error("unsupported WAV block alignment");
    }

    AudioBuffer out;
    out.sample_rate = fmt.sample_rate;
    out.channels = fmt.channels;
    const size_t frames = data_size / fmt.block_align;
    out.samples.reserve(frames * fmt.channels);
    for (size_t frame = 0; frame < frames; ++frame) {
        const uint8_t* base = wav.data() + data_pos + frame * fmt.block_align;
        for (uint16_t ch = 0; ch < fmt.channels; ++ch) {
            const uint8_t* sample = base + size_t(ch) * bytes;
            double v = 0.0;
            if (fmt.audio_format == kWavePcm) {
                v = double(read_signed_le(sample, fmt.bits_per_sample)) /
                    pcm_scale(fmt.bits_per_sample);
            } else if (fmt.audio_format == kWaveFloat) {
                v = read_float_le(sample, fmt.bits_per_sample);
            } else {
                throw std::runtime_error("unsupported WAV format");
            }
            out.samples.push_back(clamp_i16(v));
        }
    }
    return out;
}

void write_wav_pcm16(const std::string& path, const AudioBuffer& audio) {
    if (audio.channels == 0 || audio.sample_rate == 0) throw std::runtime_error("bad audio format");
    if (audio.samples.size() % audio.channels != 0) {
        throw std::runtime_error("sample count is not frame-aligned");
    }
    const uint32_t data_bytes = uint32_t(audio.samples.size() * sizeof(int16_t));
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open output WAV: " + path);
    f.write("RIFF", 4);
    put_u32(f, 36 + data_bytes);
    f.write("WAVEfmt ", 8);
    put_u32(f, 16);
    put_u16(f, kWavePcm);
    put_u16(f, audio.channels);
    put_u32(f, audio.sample_rate);
    put_u32(f, audio.sample_rate * audio.channels * 2);
    put_u16(f, uint16_t(audio.channels * 2));
    put_u16(f, 16);
    f.write("data", 4);
    put_u32(f, data_bytes);
    for (int16_t s : audio.samples) put_u16(f, uint16_t(s));
}

AudioBuffer convert_audio(const AudioBuffer& in,
                          uint32_t sample_rate,
                          uint16_t channels,
                          double volume) {
    if (sample_rate == 0 || channels == 0) throw std::runtime_error("bad target format");
    if (volume < 0.0) throw std::runtime_error("volume must be >= 0");
    if (in.channels == 0 || in.sample_rate == 0) throw std::runtime_error("bad input format");
    const size_t in_frames = in.samples.size() / in.channels;
    const size_t out_frames = (in.sample_rate == sample_rate)
                                  ? in_frames
                                  : std::max<size_t>(1, size_t(std::llround(
                                                            double(in_frames) *
                                                            double(sample_rate) /
                                                            double(in.sample_rate))));
    AudioBuffer out;
    out.sample_rate = sample_rate;
    out.channels = channels;
    out.samples.reserve(out_frames * channels);
    for (size_t i = 0; i < out_frames; ++i) {
        const double src_pos = double(i) * double(in.sample_rate) / double(sample_rate);
        const size_t i0 = std::min<size_t>(size_t(src_pos), in_frames ? in_frames - 1 : 0);
        const size_t i1 = std::min<size_t>(i0 + 1, in_frames ? in_frames - 1 : 0);
        const double frac = src_pos - double(i0);
        double mono0 = 0.0;
        double mono1 = 0.0;
        if (in_frames > 0) {
            for (uint16_t ch = 0; ch < in.channels; ++ch) {
                mono0 += double(in.samples[i0 * in.channels + ch]) / 32768.0;
                mono1 += double(in.samples[i1 * in.channels + ch]) / 32768.0;
            }
            mono0 /= in.channels;
            mono1 /= in.channels;
        }
        const int16_t s = clamp_i16((mono0 + (mono1 - mono0) * frac) * volume);
        for (uint16_t ch = 0; ch < channels; ++ch) out.samples.push_back(s);
    }
    return out;
}

std::string describe_audio(const AudioBuffer& audio) {
    std::ostringstream ss;
    const size_t frames = audio.channels ? audio.samples.size() / audio.channels : 0;
    ss << frames << " frames, " << audio.sample_rate << " Hz, " << audio.channels
       << " channel(s), PCM16, " << (audio.sample_rate ? double(frames) / audio.sample_rate : 0.0)
       << " s";
    return ss.str();
}

}  // namespace radio
