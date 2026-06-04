/*
  chirp_modem.cpp - experimental CSS/chirp modem prototype.

  This is a separate modem from p4modem.cpp. It uses LoRa-style cyclic shifts of
  an up-chirp, then decodes with a drift-aware correlator. The receiver searches
  the sync sequence over several candidate symbol durations and keeps tracking
  timing drift from neighboring symbol decisions.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr int SAMPLE_RATE = 8000;
static constexpr int SYMBOL_SAMPLES = 128;
static constexpr int ALPHABET = 16;
static constexpr int BITS_PER_SYMBOL = 4;
static constexpr int PREAMBLE_SYMBOLS = 48;
static constexpr int SYNC_SYMBOLS = 8;
static constexpr int FEC_CODEWORD_BITS = 100;
static constexpr double PI = 3.14159265358979323846;
static constexpr double FREQ_LOW = 700.0;
static constexpr double FREQ_HIGH = 2300.0;
static constexpr double AMP = 0.55;

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

static std::vector<uint8_t> bytes_to_nibbles(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> nibbles;
    nibbles.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        nibbles.push_back((b >> 4) & 0x0F);
        nibbles.push_back(b & 0x0F);
    }
    return nibbles;
}

static std::vector<uint8_t> nibbles_to_bytes(const std::vector<uint8_t>& nibbles) {
    std::vector<uint8_t> bytes((nibbles.size() + 1) / 2, 0);
    for (size_t i = 0; i < nibbles.size(); ++i) {
        if ((i % 2) == 0) {
            bytes[i / 2] |= uint8_t((nibbles[i] & 0x0F) << 4);
        } else {
            bytes[i / 2] |= uint8_t(nibbles[i] & 0x0F);
        }
    }
    return bytes;
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
        bytes[i / 8] |= uint8_t(bits[i] & 1) << (7 - (i % 8));
    }
    return bytes;
}

static std::vector<uint8_t> bits_to_symbols(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> symbols;
    symbols.reserve((bits.size() + BITS_PER_SYMBOL - 1) / BITS_PER_SYMBOL);
    for (size_t i = 0; i < bits.size(); i += BITS_PER_SYMBOL) {
        uint8_t symbol = 0;
        for (int j = 0; j < BITS_PER_SYMBOL; ++j) {
            symbol <<= 1;
            if (i + size_t(j) < bits.size()) symbol |= bits[i + size_t(j)] & 1;
        }
        symbols.push_back(symbol);
    }
    return symbols;
}

static std::vector<uint8_t> symbols_to_bits(const std::vector<uint8_t>& symbols) {
    std::vector<uint8_t> bits;
    bits.reserve(symbols.size() * BITS_PER_SYMBOL);
    for (uint8_t symbol : symbols) {
        for (int i = BITS_PER_SYMBOL - 1; i >= 0; --i) {
            bits.push_back((symbol >> i) & 1);
        }
    }
    return bits;
}

class LDPCCodec {
    static constexpr int K = 82;
    static constexpr int N = FEC_CODEWORD_BITS;
    static constexpr int P = N - K;
    static constexpr int MAX_ITER = 8;

public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& info_bits) {
        std::vector<uint8_t> coded;
        for (size_t pos = 0; pos < info_bits.size(); pos += K) {
            std::vector<uint8_t> chunk(K, 0);
            for (int i = 0; i < K && pos + i < info_bits.size(); ++i) {
                chunk[i] = info_bits[pos + i];
            }
            auto codeword = encode_chunk(chunk);
            coded.insert(coded.end(), codeword.begin(), codeword.end());
        }
        return coded;
    }

    static std::vector<uint8_t> decode(const std::vector<uint8_t>& received_bits) {
        std::vector<uint8_t> decoded;
        for (size_t pos = 0; pos < received_bits.size(); pos += N) {
            std::vector<uint8_t> chunk(N, 0);
            for (int i = 0; i < N && pos + i < received_bits.size(); ++i) {
                chunk[i] = received_bits[pos + i];
            }
            auto decoded_chunk = decode_chunk(chunk);
            decoded.insert(decoded.end(), decoded_chunk.begin(), decoded_chunk.end());
        }
        return decoded;
    }

private:
    static std::vector<std::vector<int>> get_h_matrix() {
        std::vector<std::vector<int>> H(P);
        for (int n = 0; n < K; ++n) {
            H[(n * 3) % P].push_back(n);
            H[(n * 3 + 1) % P].push_back(n);
            H[(n * 3 + 2) % P].push_back(n);
        }
        for (int p = 0; p < P; ++p) {
            H[p].push_back(K + p);
        }
        return H;
    }

    static std::vector<uint8_t> encode_chunk(const std::vector<uint8_t>& info) {
        auto H = get_h_matrix();
        std::vector<uint8_t> codeword = info;
        codeword.resize(N, 0);
        for (int p = 0; p < P; ++p) {
            uint8_t parity = 0;
            for (int n : H[p]) {
                if (n < K) parity ^= info[n];
            }
            codeword[K + p] = parity;
        }
        return codeword;
    }

    static std::vector<uint8_t> decode_chunk(const std::vector<uint8_t>& received) {
        auto H = get_h_matrix();
        std::vector<uint8_t> result = received;

        for (int iter = 0; iter < MAX_ITER; ++iter) {
            std::vector<uint8_t> syndrome(P, 0);
            int unsatisfied = 0;
            for (int p = 0; p < P; ++p) {
                uint8_t parity = 0;
                for (int n : H[p]) parity ^= result[n] & 1;
                syndrome[p] = parity;
                unsatisfied += parity;
            }
            if (unsatisfied == 0) break;

            std::vector<int> votes(K, 0);
            for (int p = 0; p < P; ++p) {
                if (!syndrome[p]) continue;
                for (int n : H[p]) {
                    if (n < K) votes[n]++;
                }
            }

            int best_bit = -1;
            int best_votes = 0;
            for (int i = 0; i < K; ++i) {
                if (votes[i] > best_votes) {
                    best_votes = votes[i];
                    best_bit = i;
                }
            }

            if (best_bit < 0 || best_votes < 2) break;
            result[best_bit] ^= 1;
        }

        std::vector<uint8_t> decoded(K);
        for (int i = 0; i < K; ++i) decoded[i] = result[i];
        return decoded;
    }
};

static std::vector<uint8_t> ldpc_encode(const std::vector<uint8_t>& bits) {
    return LDPCCodec::encode(bits);
}

static std::vector<uint8_t> ldpc_decode(const std::vector<uint8_t>& bits) {
    return LDPCCodec::decode(bits);
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

static std::vector<double> make_base_chirp() {
    std::vector<double> chirp(SYMBOL_SAMPLES);
    const double duration = double(SYMBOL_SAMPLES) / SAMPLE_RATE;
    const double sweep = (FREQ_HIGH - FREQ_LOW) / duration;

    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        const double t = double(n) / SAMPLE_RATE;
        const double phase = 2.0 * PI * (FREQ_LOW * t + 0.5 * sweep * t * t);
        chirp[n] = AMP * std::cos(phase);
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
    return wave[i0] + (wave[i1] - wave[i0]) * frac;
}

static std::vector<double> make_symbol_wave(double symbol) {
    static const auto base = make_base_chirp();
    const double shift = symbol * double(SYMBOL_SAMPLES) / ALPHABET;
    std::vector<double> out(SYMBOL_SAMPLES);
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        out[n] = cyclic_sample(base, double(n) + shift);
    }
    return out;
}

static const std::vector<double>& symbol_template(int symbol, int offset_index) {
    static const double offsets[] = {-0.25, 0.0, 0.25};
    static const std::vector<std::vector<double>> templates = [] {
        std::vector<std::vector<double>> out;
        out.reserve(ALPHABET * 3);
        for (int symbol = 0; symbol < ALPHABET; ++symbol) {
            for (double offset : offsets) {
                out.push_back(make_symbol_wave(double(symbol) + offset));
            }
        }
        return out;
    }();
    return templates[size_t(symbol * 3 + offset_index)];
}

static void append_symbol_pcm(std::vector<int16_t>& pcm, int symbol) {
    const auto wave = make_symbol_wave(double(symbol & 0x0F));
    for (double x : wave) {
        int v = int(std::round(x * 32767.0));
        v = std::clamp(v, -32768, 32767);
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
    double samples[SYMBOL_SAMPLES];
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double p = pos + double(i) * symbol_span / SYMBOL_SAMPLES;
        samples[i] = sample_at(pcm, p);
        mean += samples[i];
    }
    mean /= SYMBOL_SAMPLES;

    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        const double a = samples[i] - mean;
        const double b = tpl[i] * 32767.0;
        dot += a * b;
        e1 += a * a;
        e2 += b * b;
    }
    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

struct Decision {
    int symbol = 0;
    double score = -1.0;
    double timing_offset = 0.0;
};

static Decision decode_symbol_at(const std::vector<int16_t>& pcm,
                                 double pos,
                                 double symbol_span,
                                 bool intermediate) {
    static const double timing_offsets[] = {
        -24.0, -18.0, -12.0, -8.0, -4.0, -2.0, -1.0, -0.5,
        0.0,
        0.5, 1.0, 2.0, 4.0, 8.0, 12.0, 18.0, 24.0
    };
    Decision best;
    double best_rank = -1.0;
    for (double timing_offset : timing_offsets) {
        for (int symbol = 0; symbol < ALPHABET; ++symbol) {
            if (intermediate) {
                for (int offset_index = 0; offset_index < 3; ++offset_index) {
                    const auto& tpl = symbol_template(symbol, offset_index);
                    const double score = corr_score(pcm, pos + timing_offset, symbol_span, tpl);
                    const double rank = score - 0.003 * std::abs(timing_offset);
                    if (rank > best_rank) {
                        best = {symbol, score, timing_offset};
                        best_rank = rank;
                    }
                }
            } else {
                const auto& tpl = symbol_template(symbol, 1);
                const double score = corr_score(pcm, pos + timing_offset, symbol_span, tpl);
                const double rank = score - 0.003 * std::abs(timing_offset);
                if (rank > best_rank) {
                    best = {symbol, score, timing_offset};
                    best_rank = rank;
                }
            }
        }
    }
    return best;
}

static void encode_file(const std::string& in_path, const std::string& out_pcm_path) {
    auto payload = read_file(in_path);
    if (payload.size() > 4096) throw std::runtime_error("Prototype limit: input max 4096 bytes");

    std::vector<uint8_t> frame;
    frame.push_back(uint8_t(payload.size() & 0xFF));
    frame.push_back(uint8_t((payload.size() >> 8) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());

    const uint16_t crc = crc16_ccitt(frame);
    frame.push_back(uint8_t(crc & 0xFF));
    frame.push_back(uint8_t((crc >> 8) & 0xFF));

    auto bits = bytes_to_bits(frame);
    auto fec = ldpc_encode(bits);
    auto tx_bits = interleave(fec);
    auto symbols = bits_to_symbols(tx_bits);
    std::vector<int16_t> pcm;
    for (int i = 0; i < PREAMBLE_SYMBOLS; ++i) append_symbol_pcm(pcm, 0);

    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    for (int s : sync) append_symbol_pcm(pcm, s);
    for (uint8_t s : symbols) append_symbol_pcm(pcm, s);
    pcm.insert(pcm.end(), SAMPLE_RATE / 4, 0);

    write_pcm16(out_pcm_path, pcm);
    std::cerr << "Encoded " << payload.size() << " bytes into "
              << pcm.size() << " PCM samples, duration "
              << double(pcm.size()) / SAMPLE_RATE << " s\n";
}

struct SyncLock {
    double sync_pos = 0.0;
    double symbol_span = SYMBOL_SAMPLES;
    double score = -1.0;
};

static SyncLock find_sync(const std::vector<int16_t>& pcm) {
    const int sync[SYNC_SYMBOLS] = {15, 1, 14, 2, 13, 3, 12, 4};
    SyncLock best;

    for (int scale = 90; scale <= 110; ++scale) {
        const double span = double(SYMBOL_SAMPLES) * double(scale) / 100.0;
        const double expected = PREAMBLE_SYMBOLS * span;
        for (double delta = -span; delta <= span; delta += 4.0) {
            const double sync_pos = expected + delta;
            if (sync_pos < 0.0 || sync_pos + SYNC_SYMBOLS * span >= double(pcm.size())) continue;

            double total = 0.0;
            for (int i = 0; i < SYNC_SYMBOLS; ++i) {
                const auto& tpl = symbol_template(sync[i], 1);
                total += corr_score(pcm, sync_pos + i * span, span, tpl);
            }
            const double score = total / SYNC_SYMBOLS;
            if (score > best.score) best = {sync_pos, span, score};
        }
    }

    if (best.score < 0.15) throw std::runtime_error("Sync not found");
    std::cerr << "Sync score=" << best.score
              << ", span=" << best.symbol_span
              << ", drift=" << ((best.symbol_span / SYMBOL_SAMPLES) - 1.0) * 100.0
              << "%\n";
    return best;
}

static std::vector<uint8_t> decode_symbols_tracking(const std::vector<int16_t>& pcm,
                                                    double data_pos,
                                                    double symbol_span) {
    std::vector<uint8_t> symbols;
    double pos = data_pos;
    double span = symbol_span;
    double drift = 0.0;

    while (pos + span < double(pcm.size()) && symbols.size() < 10000) {
        const Decision d = decode_symbol_at(pcm, pos, span, true);
        symbols.push_back(uint8_t(d.symbol & 0x0F));

        // Neighboring-symbol timing tracker. A persistent positive best offset
        // means predictions are early; a negative offset means predictions are
        // late. Smooth both the next position and the symbol span estimate.
        drift = 0.75 * drift + 0.25 * d.timing_offset;
        span += 0.08 * drift;
        span = std::clamp(span, SYMBOL_SAMPLES * 0.75, SYMBOL_SAMPLES * 1.25);
        pos += span + 0.80 * d.timing_offset;
    }

    return symbols;
}

static bool extract_payload_from_symbols(const std::vector<uint8_t>& symbols,
                                         std::vector<uint8_t>* payload) {
    auto symbol_bits = symbols_to_bits(symbols);
    for (size_t fec_bit_count = FEC_CODEWORD_BITS;
         fec_bit_count <= symbol_bits.size();
         fec_bit_count += FEC_CODEWORD_BITS) {
        std::vector<uint8_t> tx_bits(symbol_bits.begin(), symbol_bits.begin() + std::ptrdiff_t(fec_bit_count));
        auto fec_bits = deinterleave(tx_bits);
        auto data_bits = ldpc_decode(fec_bits);
        auto bytes = bits_to_bytes(data_bits);
        if (bytes.size() < 4) continue;

        const uint16_t len = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
        if (len > 4096) continue;
        const size_t frame_len = 2 + size_t(len) + 2;
        if (bytes.size() < frame_len) continue;

        std::vector<uint8_t> frame(bytes.begin(), bytes.begin() + std::ptrdiff_t(frame_len - 2));
        const uint16_t got_crc = uint16_t(bytes[frame_len - 2]) |
                                 (uint16_t(bytes[frame_len - 1]) << 8);
        if (got_crc == crc16_ccitt(frame)) {
            payload->assign(bytes.begin() + 2, bytes.begin() + std::ptrdiff_t(2 + len));
            return true;
        }
    }
    return false;
}

static void decode_file(const std::string& in_pcm_path, const std::string& out_path) {
    auto pcm = read_pcm16(in_pcm_path);
    if (pcm.size() < size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * SYMBOL_SAMPLES)) {
        throw std::runtime_error("PCM too short");
    }

    const SyncLock lock = find_sync(pcm);
    const double data_pos = lock.sync_pos + SYNC_SYMBOLS * lock.symbol_span;

    std::vector<uint8_t> payload;
    bool found = false;
    for (int ppm = 0; ppm <= 20 && !found; ++ppm) {
        const int signs[] = {1, -1};
        for (int sign : signs) {
            const int signed_ppm = ppm == 0 ? 0 : sign * ppm;
            if (ppm == 0 && sign < 0) continue;
            const double candidate_span = lock.symbol_span * (1.0 + double(signed_ppm) / 1000.0);
            auto symbols = decode_symbols_tracking(pcm, data_pos, candidate_span);
            if (extract_payload_from_symbols(symbols, &payload)) {
                std::cerr << "Data drift hypothesis=" << double(signed_ppm) / 10.0 << "%\n";
                found = true;
                break;
            }
        }
    }

    if (!found) {
        for (double offset = -8.0; offset <= 8.0 && !found; offset += 2.0) {
            auto symbols = decode_symbols_tracking(pcm, data_pos + offset, lock.symbol_span);
            if (extract_payload_from_symbols(symbols, &payload)) {
                std::cerr << "Data timing offset hypothesis=" << offset << " samples\n";
                found = true;
                break;
            }
        }
    }

    if (!found) throw std::runtime_error("CRC check failed or valid frame not found");
    write_file(out_path, payload);
    std::cerr << "Decoded " << payload.size() << " bytes OK\n";
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " enc input.bin output.pcm\n"
                      << "  " << argv[0] << " dec input.pcm output.bin\n";
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
