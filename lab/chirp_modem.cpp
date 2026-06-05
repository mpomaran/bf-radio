/*
  chirp_modem.cpp - experimental CSS/chirp modem prototype.

  Portable C++17 single-file modem for 8 kHz PCM16 audio. The waveform uses
  LoRa-like cyclic shifts of an up-chirp with 16 symbols. This version keeps the
  command line interface stable, but replaces the old structurally broken FEC
  with a small systematic sparse parity-check code and feeds it soft metrics
  from the chirp correlator.

  This is still a research prototype, not a production modem.
*/

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr int SAMPLE_RATE = 8000;
static constexpr int SYMBOL_SAMPLES = 128;
static constexpr int ALPHABET = 16;
static constexpr int BITS_PER_SYMBOL = 4;
static constexpr int PREAMBLE_SYMBOLS = 48;
static constexpr int SYNC_SYMBOLS = 8;
static constexpr int FEC_INFO_BITS = 64;
static constexpr int FEC_PARITY_BITS = 64;
static constexpr int FEC_CODEWORD_BITS = FEC_INFO_BITS + FEC_PARITY_BITS;
static constexpr int MAX_PAYLOAD_BYTES = 4096;
static constexpr int PROTOCOL_HEADER_BYTES = 8;
static constexpr int CRC_BYTES = 2;
static constexpr uint8_t PROTOCOL_VERSION = 1;
static constexpr double PI = 3.14159265358979323846;
static constexpr double FREQ_LOW = 700.0;
static constexpr double FREQ_HIGH = 2300.0;
static constexpr double AMP = 0.55;
static constexpr double NOMINAL_SPAN = double(SYMBOL_SAMPLES);
static const uint8_t PROTOCOL_MAGIC[4] = {'C', 'H', 'R', 'P'};

static uint16_t crc16_ccitt(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= uint16_t(b) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
        }
    }
    return crc;
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open input file");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open output file");
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

static std::vector<int16_t> read_pcm16(const std::string& path) {
    auto bytes = read_file(path);
    if (bytes.size() % 2) bytes.pop_back();

    std::vector<int16_t> pcm;
    pcm.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2) {
        pcm.push_back(int16_t(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8)));
    }
    return pcm;
}

static void write_pcm16(const std::string& path, const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> bytes;
    bytes.reserve(pcm.size() * 2);
    for (int16_t s : pcm) {
        bytes.push_back(uint8_t(uint16_t(s) & 0xFF));
        bytes.push_back(uint8_t((uint16_t(s) >> 8) & 0xFF));
    }
    write_file(path, bytes);
}

static std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1);
    }
    return bits;
}

static std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) {
        bytes[i / 8] |= uint8_t(bits[i] & 1) << (7 - int(i % 8));
    }
    return bytes;
}

static uint8_t binary_to_gray4(uint8_t x) {
    x &= 0x0F;
    return uint8_t((x ^ (x >> 1)) & 0x0F);
}

static uint8_t gray_to_binary4(uint8_t g) {
    g &= 0x0F;
    g ^= uint8_t(g >> 1);
    g ^= uint8_t(g >> 2);
    return uint8_t(g & 0x0F);
}

static std::vector<uint8_t> bits_to_symbols(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> symbols;
    symbols.reserve((bits.size() + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL);
    for (size_t i = 0; i < bits.size(); i += BITS_PER_SYMBOL) {
        uint8_t binary_symbol = 0;
        for (int j = 0; j < BITS_PER_SYMBOL; ++j) {
            binary_symbol <<= 1;
            if (i + size_t(j) < bits.size()) binary_symbol |= bits[i + size_t(j)] & 1;
        }
        symbols.push_back(binary_to_gray4(binary_symbol));
    }
    return symbols;
}

/*
  Small systematic sparse parity-check FEC.

  This is intentionally documented as a compact experimental LDPC-style code,
  not a standards-compatible LDPC. It uses:

    K = 64 information bits
    P = 64 parity bits
    N = 128 total bits, rate 1/2

  H = [A | I], where each information column in A has weight 3:

    rows: n, 11*n+7, 23*n+19 (mod 64)

  The first row index makes all information columns unique. Parity columns are
  unit vectors, so there are no duplicate columns and no weight-1 or weight-2
  undetectable error patterns. Encoding is systematic. Decoding is an iterative
  hard/soft weighted bit-flipping decoder. Positive LLR means bit 0 is more
  likely; negative LLR means bit 1 is more likely.
*/
class LDPCCodec {
    static constexpr int K = FEC_INFO_BITS;
    static constexpr int P = FEC_PARITY_BITS;
    static constexpr int N = FEC_CODEWORD_BITS;
    static constexpr int MAX_ITER = 20;

public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& info_bits) {
        std::vector<uint8_t> coded;
        coded.reserve(((info_bits.size() + K - 1) / K) * N);
        for (size_t pos = 0; pos < info_bits.size(); pos += K) {
            std::array<uint8_t, K> chunk = {};
            for (int i = 0; i < K && pos + size_t(i) < info_bits.size(); ++i) {
                chunk[size_t(i)] = info_bits[pos + size_t(i)] & 1;
            }
            const auto codeword = encode_chunk(chunk);
            coded.insert(coded.end(), codeword.begin(), codeword.end());
        }
        return coded;
    }

    static std::vector<uint8_t> decode_from_llr(const std::vector<double>& llr_bits) {
        std::vector<uint8_t> decoded;
        decoded.reserve(((llr_bits.size() + N - 1) / N) * K);
        for (size_t pos = 0; pos < llr_bits.size(); pos += N) {
            std::array<double, N> chunk = {};
            for (int i = 0; i < N; ++i) {
                chunk[size_t(i)] = (pos + size_t(i) < llr_bits.size()) ? llr_bits[pos + size_t(i)] : 4.0;
            }
            const auto decoded_chunk = decode_chunk_from_llr(chunk);
            decoded.insert(decoded.end(), decoded_chunk.begin(), decoded_chunk.end());
        }
        return decoded;
    }

    static std::vector<uint8_t> decode_hard(const std::vector<uint8_t>& bits) {
        std::vector<double> llr;
        llr.reserve(bits.size());
        for (uint8_t b : bits) llr.push_back((b & 1) ? -4.0 : 4.0);
        return decode_from_llr(llr);
    }

    static bool has_unique_nonzero_columns() {
        std::vector<uint64_t> columns;
        columns.reserve(N);
        for (int n = 0; n < K; ++n) columns.push_back(info_mask(n));
        for (int p = 0; p < P; ++p) columns.push_back(uint64_t(1) << p);

        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i] == 0) return false;
            for (size_t j = i + 1; j < columns.size(); ++j) {
                if (columns[i] == columns[j]) return false;
            }
        }
        return true;
    }

private:
    static uint64_t info_mask(int n) {
        const int r0 = n & 63;
        const int r1 = (11 * n + 7) & 63;
        const int r2 = (23 * n + 19) & 63;
        return (uint64_t(1) << r0) | (uint64_t(1) << r1) | (uint64_t(1) << r2);
    }

    static std::array<uint8_t, N> encode_chunk(const std::array<uint8_t, K>& info) {
        std::array<uint8_t, N> codeword = {};
        for (int i = 0; i < K; ++i) codeword[size_t(i)] = info[size_t(i)] & 1;

        for (int p = 0; p < P; ++p) {
            uint8_t parity = 0;
            for (int n = 0; n < K; ++n) {
                if ((info_mask(n) >> p) & 1U) parity ^= info[size_t(n)] & 1;
            }
            codeword[size_t(K + p)] = parity;
        }
        return codeword;
    }

    static int compute_syndrome(const std::array<uint8_t, N>& bits,
                                std::array<uint8_t, P>* syndrome) {
        int unsatisfied = 0;
        for (int p = 0; p < P; ++p) {
            uint8_t parity = bits[size_t(K + p)] & 1;
            for (int n = 0; n < K; ++n) {
                if ((info_mask(n) >> p) & 1U) parity ^= bits[size_t(n)] & 1;
            }
            (*syndrome)[size_t(p)] = parity;
            unsatisfied += parity;
        }
        return unsatisfied;
    }

    static std::array<uint8_t, K> decode_chunk_from_llr(const std::array<double, N>& llr) {
        std::array<uint8_t, N> bits = {};
        for (int i = 0; i < N; ++i) bits[size_t(i)] = llr[size_t(i)] < 0.0 ? 1 : 0;

        for (int iter = 0; iter < MAX_ITER; ++iter) {
            std::array<uint8_t, P> syndrome = {};
            if (compute_syndrome(bits, &syndrome) == 0) break;

            int best_bit = -1;
            double best_score = 0.0;
            for (int bit = 0; bit < N; ++bit) {
                int unsat = 0;
                int sat = 0;
                if (bit < K) {
                    const uint64_t mask = info_mask(bit);
                    for (int p = 0; p < P; ++p) {
                        if ((mask >> p) & 1U) {
                            if (syndrome[size_t(p)]) ++unsat;
                            else ++sat;
                        }
                    }
                } else {
                    if (syndrome[size_t(bit - K)]) ++unsat;
                    else ++sat;
                }

                const double reliability = std::min(std::abs(llr[size_t(bit)]), 8.0);
                const double score = double(unsat) - 0.45 * double(sat) - 0.08 * reliability;
                if (score > best_score) {
                    best_score = score;
                    best_bit = bit;
                }
            }

            if (best_bit < 0) break;
            bits[size_t(best_bit)] ^= 1;
        }

        std::array<uint8_t, K> decoded = {};
        for (int i = 0; i < K; ++i) decoded[size_t(i)] = bits[size_t(i)] & 1;
        return decoded;
    }
};

static std::vector<uint8_t> fec_encode_bits(const std::vector<uint8_t>& info_bits) {
    return LDPCCodec::encode(info_bits);
}

static std::vector<uint8_t> fec_decode_bits_from_llr(const std::vector<double>& llr_bits) {
    return LDPCCodec::decode_from_llr(llr_bits);
}

static std::vector<uint8_t> fec_decode_bits_hard(const std::vector<uint8_t>& bits) {
    return LDPCCodec::decode_hard(bits);
}

static std::vector<uint8_t> interleave(const std::vector<uint8_t>& in,
                                       int columns = FEC_CODEWORD_BITS) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t idx = row * cols + col;
            if (idx < in.size()) out.push_back(in[idx]);
        }
    }
    return out;
}

static std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in,
                                         int columns = FEC_CODEWORD_BITS) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<uint8_t> out(in.size(), 0);
    size_t src = 0;
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t dst = row * cols + col;
            if (dst < in.size()) out[dst] = in[src++];
        }
    }
    return out;
}

static std::vector<double> interleave_soft(const std::vector<double>& in,
                                           int columns = FEC_CODEWORD_BITS) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<double> out;
    out.reserve(in.size());
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t idx = row * cols + col;
            if (idx < in.size()) out.push_back(in[idx]);
        }
    }
    return out;
}

static std::vector<double> deinterleave_soft(const std::vector<double>& in,
                                             int columns = FEC_CODEWORD_BITS) {
    if (in.empty() || columns <= 1) return in;
    const size_t cols = size_t(columns);
    const size_t rows = (in.size() + cols - 1) / cols;
    std::vector<double> out(in.size(), 0.0);
    size_t src = 0;
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            const size_t dst = row * cols + col;
            if (dst < in.size()) out[dst] = in[src++];
        }
    }
    return out;
}

static std::vector<double> make_base_chirp() {
    std::vector<double> chirp(SYMBOL_SAMPLES);
    const double duration = double(SYMBOL_SAMPLES) / SAMPLE_RATE;
    const double sweep = (FREQ_HIGH - FREQ_LOW) / duration;

    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        const double t = double(n) / SAMPLE_RATE;
        const double phase = 2.0 * PI * (FREQ_LOW * t + 0.5 * sweep * t * t);
        chirp[size_t(n)] = AMP * std::cos(phase);
    }
    return chirp;
}

static double cyclic_sample(const std::vector<double>& wave, double idx) {
    const double n = double(wave.size());
    idx = std::fmod(idx, n);
    if (idx < 0.0) idx += n;
    const int i0 = int(std::floor(idx));
    const int i1 = (i0 + 1) % int(wave.size());
    const double frac = idx - i0;
    return wave[size_t(i0)] + (wave[size_t(i1)] - wave[size_t(i0)]) * frac;
}

static std::vector<double> make_symbol_wave(double symbol) {
    static const std::vector<double> base = make_base_chirp();
    const double shift = symbol * double(SYMBOL_SAMPLES) / ALPHABET;
    std::vector<double> out(SYMBOL_SAMPLES);
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        out[size_t(n)] = cyclic_sample(base, double(n) + shift);
    }
    return out;
}

static const std::vector<double>& symbol_template(int symbol, int offset_index) {
    static const double offsets[] = {-0.25, 0.0, 0.25};
    static const std::vector<std::vector<double> > templates = [] {
        std::vector<std::vector<double> > out;
        out.reserve(ALPHABET * 3);
        for (int symbol = 0; symbol < ALPHABET; ++symbol) {
            for (double offset : offsets) out.push_back(make_symbol_wave(double(symbol) + offset));
        }
        return out;
    }();
    return templates[size_t(symbol * 3 + offset_index)];
}

static void append_symbol_pcm(std::vector<int16_t>& pcm, int raw_symbol) {
    const std::vector<double> wave = make_symbol_wave(double(raw_symbol & 0x0F));
    for (double x : wave) {
        int v = int(std::round(x * 32767.0));
        v = std::max(-32768, std::min(32767, v));
        pcm.push_back(int16_t(v));
    }
}

static double sample_at(const std::vector<int16_t>& pcm, double pos) {
    if (pcm.empty()) return 0.0;
    if (pos <= 0.0) return pcm.front();
    if (pos >= double(pcm.size() - 1)) return pcm.back();
    const size_t i = size_t(pos);
    const double frac = pos - double(i);
    return double(pcm[i]) + (double(pcm[i + 1]) - double(pcm[i])) * frac;
}

static double corr_score(const std::vector<int16_t>& pcm,
                         double pos,
                         double symbol_span,
                         const std::vector<double>& tpl) {
    if (pos < 0.0 || pos + symbol_span >= double(pcm.size())) return 0.0;

    double mean = 0.0;
    std::array<double, SYMBOL_SAMPLES> samples = {};
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double p = pos + double(i) * symbol_span / SYMBOL_SAMPLES;
        samples[size_t(i)] = sample_at(pcm, p);
        mean += samples[size_t(i)];
    }
    mean /= SYMBOL_SAMPLES;

    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double a = samples[size_t(i)] - mean;
        const double b = tpl[size_t(i)] * 32767.0;
        dot += a * b;
        e1 += a * a;
        e2 += b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

struct SymbolMetrics {
    std::array<double, ALPHABET> metric;
    double best_score;
    double second_best_score;
    int best_symbol;
    double timing_offset;

    SymbolMetrics()
        : metric(), best_score(-1.0), second_best_score(-1.0),
          best_symbol(0), timing_offset(0.0) {}
};

static void finalize_best_scores(SymbolMetrics* m) {
    m->best_score = -1.0;
    m->second_best_score = -1.0;
    m->best_symbol = 0;
    for (int s = 0; s < ALPHABET; ++s) {
        const double score = m->metric[size_t(s)];
        if (score > m->best_score) {
            m->second_best_score = m->best_score;
            m->best_score = score;
            m->best_symbol = s;
        } else if (score > m->second_best_score) {
            m->second_best_score = score;
        }
    }
}

static SymbolMetrics decode_symbol_metrics_at(const std::vector<int16_t>& pcm,
                                              double pos,
                                              double symbol_span,
                                              bool intermediate) {
    static const double timing_offsets[] = {
        -24.0, -18.0, -12.0, -8.0, -4.0, -2.0, -1.0, -0.5,
        0.0,
        0.5, 1.0, 2.0, 4.0, 8.0, 12.0, 18.0, 24.0
    };

    SymbolMetrics best;
    double best_rank = -1.0;
    for (double timing_offset : timing_offsets) {
        SymbolMetrics current;
        current.timing_offset = timing_offset;
        for (int s = 0; s < ALPHABET; ++s) {
            double score = -1.0;
            if (intermediate) {
                for (int offset_index = 0; offset_index < 3; ++offset_index) {
                    score = std::max(score, corr_score(pcm, pos + timing_offset,
                                                       symbol_span,
                                                       symbol_template(s, offset_index)));
                }
            } else {
                score = corr_score(pcm, pos + timing_offset, symbol_span, symbol_template(s, 1));
            }
            current.metric[size_t(s)] = score;
        }
        finalize_best_scores(&current);

        const double rank = current.best_score - 0.003 * std::abs(timing_offset);
        if (rank > best_rank) {
            best = current;
            best_rank = rank;
        }
    }
    return best;
}

static std::array<double, BITS_PER_SYMBOL> symbol_metrics_to_llr(const SymbolMetrics& m) {
    std::array<double, BITS_PER_SYMBOL> llr = {};
    for (int bit = 0; bit < BITS_PER_SYMBOL; ++bit) {
        double best0 = -std::numeric_limits<double>::infinity();
        double best1 = -std::numeric_limits<double>::infinity();
        for (int raw = 0; raw < ALPHABET; ++raw) {
            const uint8_t binary_symbol = gray_to_binary4(uint8_t(raw));
            const int value = (binary_symbol >> (BITS_PER_SYMBOL - 1 - bit)) & 1;
            if (value == 0) best0 = std::max(best0, m.metric[size_t(raw)]);
            else best1 = std::max(best1, m.metric[size_t(raw)]);
        }

        // Positive LLR means binary bit 0 is more likely; negative means bit 1.
        llr[size_t(bit)] = best0 - best1;
    }
    return llr;
}

static size_t fec_bits_for_info_bytes(size_t info_bytes) {
    const size_t info_bits = info_bytes * 8;
    return ((info_bits + FEC_INFO_BITS - 1) / FEC_INFO_BITS) * FEC_CODEWORD_BITS;
}

static std::vector<uint8_t> build_protected_frame(const std::vector<uint8_t>& payload,
                                                  const uint8_t magic[4],
                                                  uint8_t version,
                                                  uint8_t flags) {
    if (payload.size() > MAX_PAYLOAD_BYTES) {
        throw std::runtime_error("Prototype limit: input max 4096 bytes");
    }

    std::vector<uint8_t> frame;
    frame.push_back(magic[0]);
    frame.push_back(magic[1]);
    frame.push_back(magic[2]);
    frame.push_back(magic[3]);
    frame.push_back(version);
    frame.push_back(uint8_t(payload.size() & 0xFF));
    frame.push_back(uint8_t((payload.size() >> 8) & 0xFF));
    frame.push_back(flags);

    std::vector<uint8_t> crc_input = frame;
    crc_input.insert(crc_input.end(), payload.begin(), payload.end());
    frame.insert(frame.end(), payload.begin(), payload.end());

    const uint16_t crc = crc16_ccitt(crc_input);
    frame.push_back(uint8_t(crc & 0xFF));
    frame.push_back(uint8_t((crc >> 8) & 0xFF));
    return frame;
}

static bool parse_protected_header(const std::vector<uint8_t>& bytes,
                                   uint16_t* payload_len,
                                   size_t* required_fec_bits) {
    if (bytes.size() < PROTOCOL_HEADER_BYTES) return false;
    for (int i = 0; i < 4; ++i) {
        if (bytes[size_t(i)] != PROTOCOL_MAGIC[i]) return false;
    }
    if (bytes[4] != PROTOCOL_VERSION) return false;
    const uint16_t len = uint16_t(bytes[5]) | (uint16_t(bytes[6]) << 8);
    if (len > MAX_PAYLOAD_BYTES) return false;
    if (bytes[7] != 0) return false;

    if (payload_len) *payload_len = len;
    if (required_fec_bits) {
        *required_fec_bits = FEC_CODEWORD_BITS + fec_bits_for_info_bytes(size_t(len) + CRC_BYTES);
    }
    return true;
}

static bool parse_protected_frame(const std::vector<uint8_t>& bytes,
                                  std::vector<uint8_t>* payload) {
    uint16_t len = 0;
    if (!parse_protected_header(bytes, &len, nullptr)) return false;

    const size_t frame_len = PROTOCOL_HEADER_BYTES + size_t(len) + CRC_BYTES;
    if (bytes.size() < frame_len) return false;
    std::vector<uint8_t> frame(bytes.begin(), bytes.begin() + std::ptrdiff_t(PROTOCOL_HEADER_BYTES + len));
    const uint16_t got_crc = uint16_t(bytes[frame_len - 2]) |
                             (uint16_t(bytes[frame_len - 1]) << 8);
    if (got_crc != crc16_ccitt(frame)) return false;
    payload->assign(bytes.begin() + PROTOCOL_HEADER_BYTES,
                    bytes.begin() + std::ptrdiff_t(PROTOCOL_HEADER_BYTES + len));
    return true;
}

static std::vector<int16_t> encode_frame_bytes_to_pcm(const std::vector<uint8_t>& frame) {
    if (frame.size() < PROTOCOL_HEADER_BYTES + CRC_BYTES) {
        throw std::runtime_error("Protected frame too short");
    }

    std::vector<uint8_t> header(frame.begin(), frame.begin() + PROTOCOL_HEADER_BYTES);
    std::vector<uint8_t> body(frame.begin() + PROTOCOL_HEADER_BYTES, frame.end());
    const std::vector<uint8_t> header_fec = fec_encode_bits(bytes_to_bits(header));
    const std::vector<uint8_t> body_fec = fec_encode_bits(bytes_to_bits(body));
    const std::vector<uint8_t> body_tx_bits = interleave(body_fec);

    std::vector<uint8_t> tx_bits;
    tx_bits.reserve(header_fec.size() + body_tx_bits.size());
    tx_bits.insert(tx_bits.end(), header_fec.begin(), header_fec.end());
    tx_bits.insert(tx_bits.end(), body_tx_bits.begin(), body_tx_bits.end());
    const std::vector<uint8_t> symbols = bits_to_symbols(tx_bits);

    std::vector<int16_t> pcm;
    pcm.reserve((PREAMBLE_SYMBOLS + SYNC_SYMBOLS + symbols.size()) * SYMBOL_SAMPLES + SAMPLE_RATE / 4);
    for (int i = 0; i < PREAMBLE_SYMBOLS; ++i) append_symbol_pcm(pcm, 0);

    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int s : sync) append_symbol_pcm(pcm, s);
    for (uint8_t s : symbols) append_symbol_pcm(pcm, s);
    pcm.insert(pcm.end(), SAMPLE_RATE / 4, 0);
    return pcm;
}

static std::vector<int16_t> encode_payload_to_pcm(const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> frame =
        build_protected_frame(payload, PROTOCOL_MAGIC, PROTOCOL_VERSION, 0);
    return encode_frame_bytes_to_pcm(frame);
}

static void encode_file(const std::string& in_path, const std::string& out_pcm_path) {
    const std::vector<uint8_t> payload = read_file(in_path);
    const std::vector<int16_t> pcm = encode_payload_to_pcm(payload);
    write_pcm16(out_pcm_path, pcm);
    std::cerr << "Encoded " << payload.size() << " bytes into "
              << pcm.size() << " PCM samples, duration "
              << double(pcm.size()) / SAMPLE_RATE << " s\n";
}

struct SyncLock {
    double preamble_pos;
    double sync_pos;
    double symbol_span;
    double score;

    SyncLock()
        : preamble_pos(0.0), sync_pos(0.0), symbol_span(NOMINAL_SPAN), score(-1.0) {}
};

static double sync_score_at(const std::vector<int16_t>& pcm,
                            double preamble_pos,
                            double span) {
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    const double sync_pos = preamble_pos + PREAMBLE_SYMBOLS * span;
    if (preamble_pos < 0.0 ||
        sync_pos + SYNC_SYMBOLS * span >= double(pcm.size())) {
        return -1.0;
    }

    double total = 0.0;
    for (int i = 0; i < SYNC_SYMBOLS; ++i) {
        total += corr_score(pcm, sync_pos + i * span, span, symbol_template(sync[i], 1));
    }

    // A few preamble checks suppress false locks in arbitrary leading audio.
    const int probes[] = {0, 12, 24, 36};
    for (int idx : probes) {
        total += 0.35 * corr_score(pcm, preamble_pos + idx * span, span, symbol_template(0, 1));
    }
    return total / double(SYNC_SYMBOLS + 4 * 0.35);
}

static void consider_sync_candidate(const std::vector<int16_t>& pcm,
                                    double preamble_pos,
                                    double span,
                                    SyncLock* best) {
    const double score = sync_score_at(pcm, preamble_pos, span);
    if (score > best->score + 1e-6 ||
        (score > best->score - 0.02 && preamble_pos < best->preamble_pos)) {
        best->preamble_pos = preamble_pos;
        best->sync_pos = preamble_pos + PREAMBLE_SYMBOLS * span;
        best->symbol_span = span;
        best->score = score;
    }
}

static double find_energy_onset(const std::vector<int16_t>& pcm) {
    const size_t window = SYMBOL_SAMPLES / 2;
    if (pcm.size() <= window) return 0.0;
    for (size_t i = 0; i + window < pcm.size(); i += 8) {
        double avg_abs = 0.0;
        for (size_t j = 0; j < window; ++j) avg_abs += std::abs(double(pcm[i + j]));
        avg_abs /= double(window);
        if (avg_abs > 1500.0) return double(i);
    }
    return 0.0;
}

static SyncLock find_sync(const std::vector<int16_t>& pcm, bool verbose = true) {
    SyncLock best;
    if (pcm.size() < size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * SYMBOL_SAMPLES)) {
        throw std::runtime_error("PCM too short");
    }

    const double onset = find_energy_onset(pcm);
    for (int scale = 88; scale <= 112; scale += 2) {
        const double span = NOMINAL_SPAN * double(scale) / 100.0;
        for (double pre = onset - 0.75 * span; pre <= onset + 0.75 * span; pre += 2.0) {
            consider_sync_candidate(pcm, pre, span, &best);
        }
    }

    // The energy-onset search is fast and handles normal recordings with
    // leading silence. Fall back to a full sliding scan only if that local lock
    // is weak, which keeps long frames reasonable on Raspberry Pi-class CPUs.
    if (best.score < 0.25) {
        const int coarse_step = SYMBOL_SAMPLES / 2;
        for (int scale = 88; scale <= 112; scale += 4) {
            const double span = NOMINAL_SPAN * double(scale) / 100.0;
            const double frame_prefix = (PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * span;
            if (frame_prefix >= double(pcm.size())) continue;
            for (double pre = 0.0; pre + frame_prefix < double(pcm.size()); pre += coarse_step) {
                consider_sync_candidate(pcm, pre, span, &best);
            }
        }
    }

    SyncLock refined = best;
    for (double span = best.symbol_span - 6.0; span <= best.symbol_span + 6.0; span += 1.0) {
        if (span < NOMINAL_SPAN * 0.85 || span > NOMINAL_SPAN * 1.15) continue;
        for (double pre = best.preamble_pos - SYMBOL_SAMPLES; pre <= best.preamble_pos + SYMBOL_SAMPLES; pre += 4.0) {
            consider_sync_candidate(pcm, pre, span, &refined);
        }
    }

    if (refined.score < 0.14) throw std::runtime_error("Sync not found");
    if (verbose) {
        std::cerr << "Sync score=" << refined.score
                  << ", preamble_pos=" << refined.preamble_pos
                  << ", span=" << refined.symbol_span
                  << ", drift=" << ((refined.symbol_span / NOMINAL_SPAN) - 1.0) * 100.0
                  << "%\n";
    }
    return refined;
}

struct TimingState {
    double pos;
    double span;
    double timing_error_filtered;

    TimingState(double p, double s) : pos(p), span(s), timing_error_filtered(0.0) {}
};

static std::vector<double> decode_llrs_tracking(const std::vector<int16_t>& pcm,
                                                double data_pos,
                                                double symbol_span,
                                                size_t max_bits = 50000 * BITS_PER_SYMBOL) {
    std::vector<double> llrs;
    TimingState timing(data_pos, symbol_span);
    const double min_span = NOMINAL_SPAN * 0.85;
    const double max_span = NOMINAL_SPAN * 1.15;
    const double confidence_threshold = 0.035;
    const double kp = 0.55;
    const double ki = 0.015;

    while (timing.pos + timing.span < double(pcm.size()) && llrs.size() < max_bits) {
        const SymbolMetrics m = decode_symbol_metrics_at(pcm, timing.pos, timing.span, true);
        const std::array<double, BITS_PER_SYMBOL> symbol_llr = symbol_metrics_to_llr(m);
        for (double v : symbol_llr) llrs.push_back(v);

        const double confidence = m.best_score - m.second_best_score;
        if (confidence > confidence_threshold) {
            /*
              Sign convention:
              - m.timing_offset is the offset, in samples, that maximized the
                current symbol correlation.
              - Positive error means the best correlation is later than the
                predicted position, so the prediction was early.
              - pos advances by span plus a proportional correction.
              - span receives only a small integral correction from filtered
                timing error and is clamped to avoid runaway after a bad symbol.
            */
            timing.timing_error_filtered =
                0.85 * timing.timing_error_filtered + 0.15 * m.timing_offset;
            timing.span += ki * timing.timing_error_filtered;
            timing.span = std::max(min_span, std::min(max_span, timing.span));
            timing.pos += timing.span + kp * m.timing_offset;
        } else {
            timing.timing_error_filtered *= 0.98;
            timing.pos += timing.span;
        }
    }
    return llrs;
}

enum class StreamScanStatus {
    NoFrameWindowConsumed,
    NeedMoreSamples,
    FrameDecoded,
    InvalidFrameRejected
};

struct StreamScanResult {
    StreamScanStatus status;

    /*
      Number of samples from the front of this PCM window that a streaming
      caller may erase. If this is zero, keep the whole window and append more
      samples before scanning again.

      frame_start_sample and frame_end_sample are offsets inside the supplied
      window. frame_end_sample is exclusive and marks the end of the decoded
      chirp data symbols, not necessarily trailing silence after the frame.
    */
    size_t discard_prefix_samples;
    size_t frame_start_sample;
    size_t frame_end_sample;
    double acquisition_score;
    double estimated_symbol_span;
    std::vector<uint8_t> payload;

    StreamScanResult()
        : status(StreamScanStatus::NoFrameWindowConsumed),
          discard_prefix_samples(0), frame_start_sample(0), frame_end_sample(0),
          acquisition_score(0.0), estimated_symbol_span(0.0), payload() {}
};

static size_t clamp_discard(size_t value, size_t pcm_size) {
    return std::min(value, pcm_size);
}

static bool find_first_energy_sample(const std::vector<int16_t>& pcm, size_t* sample) {
    const size_t window = SYMBOL_SAMPLES / 2;
    if (pcm.size() <= window) return false;
    for (size_t i = 0; i + window < pcm.size(); i += 8) {
        double avg_abs = 0.0;
        for (size_t j = 0; j < window; ++j) avg_abs += std::abs(double(pcm[i + j]));
        avg_abs /= double(window);
        if (avg_abs > 1500.0) {
            *sample = i;
            return true;
        }
    }
    return false;
}

static bool decode_exact_payload_from_llrs(const std::vector<double>& llrs,
                                           size_t fec_bit_count,
                                           std::vector<uint8_t>* payload) {
    if (llrs.size() < fec_bit_count || fec_bit_count < FEC_CODEWORD_BITS) return false;

    std::vector<double> header_llrs(llrs.begin(),
                                    llrs.begin() + std::ptrdiff_t(FEC_CODEWORD_BITS));
    const std::vector<uint8_t> header_bits = fec_decode_bits_from_llr(header_llrs);
    std::vector<uint8_t> bytes = bits_to_bytes(header_bits);
    bytes.resize(PROTOCOL_HEADER_BYTES);

    size_t required_fec_bits = 0;
    if (!parse_protected_header(bytes, nullptr, &required_fec_bits)) return false;
    if (required_fec_bits != fec_bit_count) return false;

    const size_t body_fec_bits = fec_bit_count - FEC_CODEWORD_BITS;
    if (body_fec_bits > 0) {
        std::vector<double> body_tx_llrs(llrs.begin() + std::ptrdiff_t(FEC_CODEWORD_BITS),
                                         llrs.begin() + std::ptrdiff_t(fec_bit_count));
        const std::vector<double> body_fec_llrs = deinterleave_soft(body_tx_llrs);
        const std::vector<uint8_t> body_bits = fec_decode_bits_from_llr(body_fec_llrs);
        const std::vector<uint8_t> body_bytes = bits_to_bytes(body_bits);
        bytes.insert(bytes.end(), body_bytes.begin(), body_bytes.end());
    }
    return parse_protected_frame(bytes, payload);
}

/*
  Streaming acquisition API.

  Call this repeatedly with the current rolling PCM buffer. On
  NoFrameWindowConsumed or InvalidFrameRejected, erase discard_prefix_samples
  from the front and keep scanning. On NeedMoreSamples, erase only
  discard_prefix_samples and append more PCM. On FrameDecoded, payload contains
  the protected frame payload and discard_prefix_samples consumes through the
  decoded frame.
*/
static StreamScanResult scan_pcm_window_for_frame(const std::vector<int16_t>& pcm) {
    StreamScanResult result;
    result.status = StreamScanStatus::NoFrameWindowConsumed;

    const size_t max_sync_prefix = size_t(std::ceil((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                   NOMINAL_SPAN * 1.15 + 2.0 * NOMINAL_SPAN));
    const size_t min_sync_prefix = size_t(std::ceil((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) *
                                                   NOMINAL_SPAN * 0.85));
    const size_t keep_tail_without_lock = max_sync_prefix;

    if (pcm.empty()) return result;

    size_t energy = 0;
    if (!find_first_energy_sample(pcm, &energy)) {
        result.status = StreamScanStatus::NoFrameWindowConsumed;
        result.discard_prefix_samples = pcm.size();
        return result;
    }

    if (pcm.size() - energy < min_sync_prefix) {
        result.status = StreamScanStatus::NeedMoreSamples;
        result.discard_prefix_samples = energy;
        return result;
    }

    SyncLock lock;
    try {
        lock = find_sync(pcm, false);
    } catch (const std::exception&) {
        if (pcm.size() <= keep_tail_without_lock) {
            result.status = StreamScanStatus::NeedMoreSamples;
            result.discard_prefix_samples = std::min(energy, pcm.size());
            return result;
        }
        result.status = StreamScanStatus::NoFrameWindowConsumed;
        result.discard_prefix_samples = pcm.size() - keep_tail_without_lock;
        return result;
    }

    result.acquisition_score = lock.score;
    result.estimated_symbol_span = lock.symbol_span;
    const size_t frame_start = size_t(std::max(0.0, std::floor(lock.preamble_pos + 0.5)));
    result.frame_start_sample = frame_start;

    const double data_pos = lock.sync_pos + SYNC_SYMBOLS * lock.symbol_span;
    bool saw_rejectable_candidate = false;
    const size_t header_symbols = FEC_CODEWORD_BITS / BITS_PER_SYMBOL;

    for (int tenths = 0; tenths <= 30; ++tenths) {
        const int signs[] = {1, -1};
        for (int sign : signs) {
            if (tenths == 0 && sign < 0) continue;
            const int signed_tenths = sign * tenths;
            const double candidate_span =
                lock.symbol_span * (1.0 + double(signed_tenths) / 1000.0);
            const double header_end = data_pos + double(header_symbols + 1) * candidate_span;
            if (header_end >= double(pcm.size())) {
                result.status = StreamScanStatus::NeedMoreSamples;
                result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                return result;
            }

            const std::vector<double> header_llrs =
                decode_llrs_tracking(pcm, data_pos, candidate_span, FEC_CODEWORD_BITS);
            if (header_llrs.size() < FEC_CODEWORD_BITS) {
                result.status = StreamScanStatus::NeedMoreSamples;
                result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                return result;
            }

            std::vector<double> header_tx(header_llrs.begin(),
                                          header_llrs.begin() + std::ptrdiff_t(FEC_CODEWORD_BITS));
            const std::vector<uint8_t> header_bits = fec_decode_bits_from_llr(header_tx);
            const std::vector<uint8_t> header_bytes = bits_to_bytes(header_bits);

            size_t required_fec_bits = 0;
            if (!parse_protected_header(header_bytes, nullptr, &required_fec_bits)) {
                saw_rejectable_candidate = true;
                continue;
            }

            const size_t required_symbols =
                (required_fec_bits + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL;
            const double frame_end = data_pos + double(required_symbols) * candidate_span;
            if (frame_end + candidate_span >= double(pcm.size())) {
                result.status = StreamScanStatus::NeedMoreSamples;
                result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                return result;
            }

            const std::vector<double> frame_llrs =
                decode_llrs_tracking(pcm, data_pos, candidate_span, required_fec_bits);
            if (frame_llrs.size() < required_fec_bits) {
                result.status = StreamScanStatus::NeedMoreSamples;
                result.discard_prefix_samples = clamp_discard(frame_start, pcm.size());
                return result;
            }

            if (decode_exact_payload_from_llrs(frame_llrs, required_fec_bits, &result.payload)) {
                result.status = StreamScanStatus::FrameDecoded;
                result.estimated_symbol_span = candidate_span;
                result.frame_end_sample = clamp_discard(size_t(std::ceil(frame_end)), pcm.size());
                result.discard_prefix_samples = result.frame_end_sample;
                return result;
            }
            saw_rejectable_candidate = true;
        }
    }

    result.status = saw_rejectable_candidate
                        ? StreamScanStatus::InvalidFrameRejected
                        : StreamScanStatus::NoFrameWindowConsumed;
    result.discard_prefix_samples =
        clamp_discard(frame_start + size_t(std::ceil(lock.symbol_span)), pcm.size());
    return result;
}

static bool decode_payload_from_pcm(const std::vector<int16_t>& pcm,
                                    std::vector<uint8_t>* payload) {
    std::vector<int16_t> buffer = pcm;
    for (int iter = 0; iter < 128 && !buffer.empty(); ++iter) {
        const StreamScanResult scan = scan_pcm_window_for_frame(buffer);
        if (scan.status == StreamScanStatus::FrameDecoded) {
            *payload = scan.payload;
            return true;
        }

        if (scan.discard_prefix_samples > 0) {
            const size_t discard = std::min(scan.discard_prefix_samples, buffer.size());
            buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            continue;
        }

        if (scan.status == StreamScanStatus::NeedMoreSamples) break;
        break;
    }
    return false;
}

static void decode_file(const std::string& in_pcm_path, const std::string& out_path) {
    const std::vector<int16_t> pcm = read_pcm16(in_pcm_path);
    std::vector<uint8_t> payload;
    if (!decode_payload_from_pcm(pcm, &payload)) {
        throw std::runtime_error("CRC check failed or valid frame not found");
    }
    write_file(out_path, payload);
    std::cerr << "Decoded " << payload.size() << " bytes OK\n";
}

static std::vector<int16_t> resample_pcm(const std::vector<int16_t>& pcm, double factor) {
    if (factor <= 0.0) throw std::runtime_error("Invalid resample factor");
    const size_t out_size = std::max<size_t>(1, size_t(std::floor(double(pcm.size()) * factor)));
    std::vector<int16_t> out;
    out.reserve(out_size);
    for (size_t i = 0; i < out_size; ++i) {
        double v = sample_at(pcm, double(i) / factor);
        v = std::max(-32768.0, std::min(32767.0, v));
        out.push_back(int16_t(std::round(v)));
    }
    return out;
}

static void append_silence(std::vector<int16_t>& pcm, int samples) {
    if (samples > 0) pcm.insert(pcm.end(), size_t(samples), 0);
}

static void append_white_noise(std::vector<int16_t>& pcm,
                               int samples,
                               double amplitude,
                               std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(-amplitude, amplitude);
    for (int i = 0; i < samples; ++i) {
        const double v = std::max(-32768.0, std::min(32767.0, dist(rng)));
        pcm.push_back(int16_t(std::round(v)));
    }
}

static void append_unrelated_chirp_burst(std::vector<int16_t>& pcm,
                                         int symbols,
                                         std::mt19937& rng) {
    std::uniform_int_distribution<int> sym_dist(0, ALPHABET - 1);
    for (int i = 0; i < symbols; ++i) append_symbol_pcm(pcm, sym_dist(rng));
}

static void append_fake_wrong_sync_transmission(std::vector<int16_t>& pcm,
                                                std::mt19937& rng) {
    for (int i = 0; i < PREAMBLE_SYMBOLS; ++i) append_symbol_pcm(pcm, 0);
    for (int i = 0; i < SYNC_SYMBOLS; ++i) append_symbol_pcm(pcm, (i * 5 + 6) & 0x0F);
    append_unrelated_chirp_burst(pcm, 20, rng);
}

static void append_fake_wrong_magic_transmission(std::vector<int16_t>& pcm,
                                                 std::mt19937& rng) {
    std::vector<uint8_t> fake_payload(24);
    for (uint8_t& b : fake_payload) b = uint8_t(rng() & 0xFFU);
    const uint8_t bad_magic[4] = {'N', 'O', 'P', 'E'};
    const std::vector<uint8_t> frame =
        build_protected_frame(fake_payload, bad_magic, PROTOCOL_VERSION, 0);
    const std::vector<int16_t> fake_pcm = encode_frame_bytes_to_pcm(frame);
    pcm.insert(pcm.end(), fake_pcm.begin(), fake_pcm.end());
}

static void append_corrupted_frame_like_burst(std::vector<int16_t>& pcm,
                                              std::mt19937& rng) {
    std::vector<uint8_t> payload(16);
    for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);
    const std::vector<int16_t> frame = encode_payload_to_pcm(payload);
    const size_t cut = std::min(frame.size(), size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS + 20) * SYMBOL_SAMPLES));
    pcm.insert(pcm.end(), frame.begin(), frame.begin() + std::ptrdiff_t(cut));
    append_white_noise(pcm, SYMBOL_SAMPLES * 4, 500.0, rng);
}

static void append_scaled(std::vector<int16_t>& dst,
                          const std::vector<int16_t>& src,
                          double gain) {
    for (int16_t s : src) {
        const double v = std::max(-32768.0, std::min(32767.0, double(s) * gain));
        dst.push_back(int16_t(std::round(v)));
    }
}

static void require_true(bool ok, const std::string& name) {
    if (!ok) throw std::runtime_error("Selftest failed: " + name);
    std::cerr << "[PASS] " << name << "\n";
}

static bool feed_stream_with_scanner(const std::vector<int16_t>& stream,
                                     size_t chunk_samples,
                                     std::vector<uint8_t>* payload,
                                     size_t* max_buffer,
                                     bool* saw_need_more,
                                     bool* saw_invalid) {
    std::vector<int16_t> buffer;
    *max_buffer = 0;
    if (saw_need_more) *saw_need_more = false;
    if (saw_invalid) *saw_invalid = false;

    for (size_t pos = 0; pos < stream.size(); pos += chunk_samples) {
        const size_t end = std::min(stream.size(), pos + chunk_samples);
        buffer.insert(buffer.end(), stream.begin() + std::ptrdiff_t(pos),
                      stream.begin() + std::ptrdiff_t(end));
        *max_buffer = std::max(*max_buffer, buffer.size());

        for (int iter = 0; iter < 8; ++iter) {
            const StreamScanResult r = scan_pcm_window_for_frame(buffer);
            if (r.status == StreamScanStatus::FrameDecoded) {
                *payload = r.payload;
                if (r.discard_prefix_samples > 0) {
                    const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                    buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
                }
                *max_buffer = std::max(*max_buffer, buffer.size());
                return true;
            }
            if (r.status == StreamScanStatus::NeedMoreSamples && saw_need_more) {
                *saw_need_more = true;
            }
            if (r.status == StreamScanStatus::InvalidFrameRejected && saw_invalid) {
                *saw_invalid = true;
            }

            if (r.discard_prefix_samples == 0) break;
            const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
            buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            *max_buffer = std::max(*max_buffer, buffer.size());
            if (buffer.empty()) break;
        }
    }

    for (int iter = 0; iter < 16 && !buffer.empty(); ++iter) {
        const StreamScanResult r = scan_pcm_window_for_frame(buffer);
        if (r.status == StreamScanStatus::FrameDecoded) {
            *payload = r.payload;
            return true;
        }
        if (r.status == StreamScanStatus::NeedMoreSamples && saw_need_more) *saw_need_more = true;
        if (r.status == StreamScanStatus::InvalidFrameRejected && saw_invalid) *saw_invalid = true;
        if (r.discard_prefix_samples == 0) break;
        const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
        buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
        *max_buffer = std::max(*max_buffer, buffer.size());
    }
    return false;
}

static void run_selftest() {
    {
        const std::vector<uint8_t> bytes = {0x00, 0x5A, 0xC3, 0xFF, 0x10};
        require_true(bits_to_bytes(bytes_to_bits(bytes)) == bytes, "bytes/bits roundtrip");
    }
    {
        for (int i = 0; i < ALPHABET; ++i) {
            require_true(gray_to_binary4(binary_to_gray4(uint8_t(i))) == i, "gray roundtrip");
        }
    }
    {
        std::vector<uint8_t> hard(257);
        for (size_t i = 0; i < hard.size(); ++i) hard[i] = uint8_t(i & 1U);
        require_true(deinterleave(interleave(hard)) == hard, "hard interleaver roundtrip");

        std::vector<double> soft(257);
        for (size_t i = 0; i < soft.size(); ++i) soft[i] = double(i) * 0.25 - 12.0;
        require_true(deinterleave_soft(interleave_soft(soft)) == soft, "soft interleaver roundtrip");
    }
    {
        require_true(LDPCCodec::has_unique_nonzero_columns(), "FEC unique nonzero columns");

        std::vector<uint8_t> info(3 * FEC_INFO_BITS);
        for (size_t i = 0; i < info.size(); ++i) info[i] = uint8_t(((i * 7U) + 3U) & 1U);
        const std::vector<uint8_t> coded = fec_encode_bits(info);

        std::vector<double> llr;
        llr.reserve(coded.size());
        for (uint8_t b : coded) llr.push_back((b & 1) ? -5.0 : 5.0);
        std::vector<uint8_t> decoded = fec_decode_bits_from_llr(llr);
        decoded.resize(info.size());
        require_true(decoded == info, "FEC no errors");

        decoded = fec_decode_bits_hard(coded);
        decoded.resize(info.size());
        require_true(decoded == info, "FEC hard API no errors");

        for (int pos : {0, 1, 7, 31, 63, 64, 91, 127}) {
            std::vector<double> damaged = llr;
            damaged[size_t(pos)] = -damaged[size_t(pos)];
            decoded = fec_decode_bits_from_llr(damaged);
            decoded.resize(FEC_INFO_BITS);
            std::vector<uint8_t> first_info(info.begin(), info.begin() + FEC_INFO_BITS);
            require_true(decoded == first_info, "FEC single-bit correction");
        }

        std::vector<double> damaged = llr;
        damaged[0] = -damaged[0];
        damaged[17] = -damaged[17];
        decoded = fec_decode_bits_from_llr(damaged);
        decoded.resize(FEC_INFO_BITS);
        std::vector<uint8_t> first_info(info.begin(), info.begin() + FEC_INFO_BITS);
        require_true(decoded == first_info, "FEC selected two-bit correction");
    }
    {
        std::mt19937 rng(12345);
        std::vector<uint8_t> payload(48);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        const std::vector<int16_t> pcm = encode_payload_to_pcm(payload);
        std::vector<uint8_t> decoded;
        require_true(decode_payload_from_pcm(pcm, &decoded) && decoded == payload, "clean modem channel");

        std::vector<int16_t> with_silence(900, 0);
        with_silence.insert(with_silence.end(), pcm.begin(), pcm.end());
        require_true(decode_payload_from_pcm(with_silence, &decoded) && decoded == payload, "leading silence");

        const double factors[] = {0.98, 0.99, 1.01, 1.02};
        for (double factor : factors) {
            const std::vector<int16_t> scaled = resample_pcm(pcm, factor);
            require_true(decode_payload_from_pcm(scaled, &decoded) && decoded == payload,
                         "time scaling");
        }
    }
    {
        std::mt19937 rng(2222);
        std::vector<uint8_t> payload(40);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        append_silence(stream, 1800);
        append_white_noise(stream, 2400, 250.0, rng);
        append_unrelated_chirp_burst(stream, 3, rng);
        append_white_noise(stream, 2200, 500.0, rng);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.95);
        append_unrelated_chirp_burst(stream, 2, rng);
        append_white_noise(stream, 1200, 300.0, rng);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 512, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload,
                     "sliding_window_stream_receiver");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "sliding receiver bounded buffer");
    }
    {
        std::mt19937 rng(3333);
        std::vector<uint8_t> payload(32);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        for (int second = 0; second < 120; ++second) {
            append_white_noise(stream, SAMPLE_RATE, 180.0, rng);
            if ((second % 40) == 7) append_unrelated_chirp_burst(stream, 1, rng);
        }
        append_silence(stream, 1600);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.90);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 4096, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload,
                     "long_idle_then_valid_frame");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "long idle bounded buffer");
    }
    {
        std::mt19937 rng(4444);
        std::vector<int16_t> stream;
        append_silence(stream, SAMPLE_RATE);
        append_white_noise(stream, SAMPLE_RATE * 3, 260.0, rng);
        append_unrelated_chirp_burst(stream, 4, rng);
        append_white_noise(stream, SAMPLE_RATE, 350.0, rng);
        append_fake_wrong_sync_transmission(stream, rng);
        append_white_noise(stream, SAMPLE_RATE, 220.0, rng);
        append_fake_wrong_magic_transmission(stream, rng);
        append_white_noise(stream, SAMPLE_RATE, 200.0, rng);
        append_corrupted_frame_like_burst(stream, rng);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(!feed_stream_with_scanner(stream, 4096, &decoded, &max_buffer,
                                              &saw_need_more, &saw_invalid),
                     "stream_no_valid_frame");
        require_true(decoded.empty(), "stream no valid payload empty");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "no-frame bounded buffer");
    }
    {
        std::mt19937 rng(5555);
        std::vector<uint8_t> payload(44);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);
        const std::vector<int16_t> frame = encode_payload_to_pcm(payload);
        const size_t partial_len =
            size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS + 40) * SYMBOL_SAMPLES);

        std::vector<int16_t> prefix;
        append_white_noise(prefix, 2400, 220.0, rng);
        std::vector<int16_t> partial = prefix;
        partial.insert(partial.end(), frame.begin(),
                       frame.begin() + std::ptrdiff_t(std::min(partial_len, frame.size())));

        std::vector<int16_t> buffer;
        std::vector<uint8_t> decoded;
        bool got_frame = false;
        bool saw_need_more = false;
        size_t max_buffer = 0;
        for (size_t pos = 0; pos < partial.size(); pos += 512) {
            const size_t end = std::min(partial.size(), pos + size_t(512));
            buffer.insert(buffer.end(), partial.begin() + std::ptrdiff_t(pos),
                          partial.begin() + std::ptrdiff_t(end));
            max_buffer = std::max(max_buffer, buffer.size());
            const StreamScanResult r = scan_pcm_window_for_frame(buffer);
            if (r.status == StreamScanStatus::FrameDecoded) got_frame = true;
            if (r.status == StreamScanStatus::NeedMoreSamples) saw_need_more = true;
            if (r.discard_prefix_samples > 0) {
                const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            }
        }
        require_true(!got_frame && saw_need_more, "stream_incomplete_frame_tail");

        for (size_t pos = std::min(partial_len, frame.size()); pos < frame.size(); pos += 512) {
            const size_t end = std::min(frame.size(), pos + size_t(512));
            buffer.insert(buffer.end(), frame.begin() + std::ptrdiff_t(pos),
                          frame.begin() + std::ptrdiff_t(end));
            max_buffer = std::max(max_buffer, buffer.size());
            for (int iter = 0; iter < 8; ++iter) {
                const StreamScanResult r = scan_pcm_window_for_frame(buffer);
                if (r.status == StreamScanStatus::FrameDecoded) {
                    decoded = r.payload;
                    got_frame = true;
                    break;
                }
                if (r.discard_prefix_samples == 0) break;
                const size_t discard = std::min(r.discard_prefix_samples, buffer.size());
                buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(discard));
            }
            if (got_frame) break;
        }
        require_true(got_frame && decoded == payload, "stream incomplete tail completed");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "incomplete stream bounded buffer");
    }
    {
        std::mt19937 rng(6666);
        std::vector<uint8_t> payload(36);
        for (uint8_t& b : payload) b = uint8_t(rng() & 0xFFU);

        std::vector<int16_t> stream;
        append_fake_wrong_magic_transmission(stream, rng);
        append_silence(stream, 1400);
        append_scaled(stream, encode_payload_to_pcm(payload), 0.92);

        std::vector<uint8_t> decoded;
        size_t max_buffer = 0;
        bool saw_need_more = false;
        bool saw_invalid = false;
        require_true(feed_stream_with_scanner(stream, 512, &decoded, &max_buffer,
                                             &saw_need_more, &saw_invalid) &&
                         decoded == payload && saw_invalid,
                     "wrong frame rejected");
        require_true(max_buffer < size_t(SAMPLE_RATE * 10), "wrong-frame bounded buffer");
    }

    std::cerr << "All self-tests passed.\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "selftest") {
            run_selftest();
            return 0;
        }

        if (argc != 4) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " enc input.bin output.pcm\n"
                      << "  " << argv[0] << " dec input.pcm output.bin\n"
                      << "  " << argv[0] << " selftest\n";
            return 1;
        }

        const std::string mode = argv[1];
        if (mode == "enc") {
            encode_file(argv[2], argv[3]);
        } else if (mode == "dec") {
            decode_file(argv[2], argv[3]);
        } else {
            throw std::runtime_error("Mode must be enc or dec");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
