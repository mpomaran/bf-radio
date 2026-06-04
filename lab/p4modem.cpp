/*
  p4modem.cpp — prosty eksperymentalny modem P4 przez PCM audio dla Baofenga/VOX

  Cel:
    - około 300 bps brutto / około 150 bps netto z prostym FEC 1/2
    - wejście/wyjście przez surowe PCM 16-bit signed little-endian mono
    - nośna audio 1500 Hz
    - symbole P4 długości 10 ms
    - alfabet 8 symboli = 3 bity/symbol
    - 100 symboli/s × 3 bity = 300 bps brutto
    - prosty FEC: repetition 2x => około 150 bps netto
    - ramki z preambułą, sync, headerem, payloadem i CRC16

  UWAGA:
    To nie jest gotowy "produkcyjny" modem. To baza do eksperymentów.
    Na prawdziwym Baofengu trzeba stroić:
      - poziom audio,
      - długość preambuły pod VOX,
      - próg detekcji,
      - symbol timing,
      - ewentualnie dodać lepszy FEC: convolutional/Viterbi albo Reed-Solomon.

  Format pracy:
    Encode:
      ./p4modem enc input.bin output.pcm

    Decode:
      ./p4modem dec input.pcm output.bin

  PCM:
    - 8000 Hz
    - mono
    - signed 16-bit little-endian
    - bez nagłówka WAV

  Stack:
    payload bytes
      ↓
    CRC16-CCITT
      ↓
    bitstream
      ↓
    FEC repetition 2x
      ↓
    block interleaver
      ↓
    grupowanie po 3 bity
      ↓
    symbole P4
      ↓
    PCM audio

  Ramka:
    PREAMBLE:
      100 symboli sync, czyli około 1 s — przy VOX można zwiększyć

    SYNC:
      16 znanych symboli

    HEADER:
      2 bajty długości payloadu, little-endian
      także chronione FEC/interleaverem w tym prototypie jako część body

    BODY:
      length[2] + payload + crc16[2]

  Modulacja P4:
    Dla symbolu generujemy sekwencję fazową P4:

      phi[n] = pi * n^2 / N

    Symbol danych wybieramy przez cykliczne przesunięcie kodu P4.
    Następnie nakładamy tę fazę na nośną audio:

      s[n] = cos(2*pi*fc*t + phi_p4[(n + shift) mod N])

    Dekoder:
      - tnie PCM na okna symbolowe,
      - koreluje każde okno z 8 wzorcami P4,
      - wybiera symbol o największej korelacji.

  Dlaczego tylko 8 symboli, a nie 64?
    64 symbole dawałyby 6 bitów/symbol, ale wymagają znacznie lepszej separacji
    korelacyjnej i synchronizacji. 8 symboli jest dużo odporniejsze na Baofenga,
    VOX i tanią kartę USB audio.

  Najważniejsze ograniczenie:
    Dekoder zakłada, że symbol timing jest trafiony po znalezieniu preambuły.
    W realnym systemie warto dodać tracking: sprawdzanie okien -1/0/+1 próbka
    i wybór największej korelacji.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr int SAMPLE_RATE = 8000;
static constexpr double CARRIER_HZ = 1500.0;
static constexpr int SYMBOL_SAMPLES = 80;       // 10 ms przy 8 kHz
static constexpr int ALPHABET = 8;              // 3 bity/symbol
static constexpr int BITS_PER_SYMBOL = 3;
static constexpr int PREAMBLE_SYMBOLS = 100;    // około 1 s dla VOX
static constexpr int SYNC_SYMBOLS = 16;
static constexpr double AMP = 0.55;

struct Complex {
    double re = 0.0;
    double im = 0.0;
};

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
        uint16_t lo = bytes[i];
        uint16_t hi = bytes[i + 1];
        pcm.push_back(int16_t(lo | (hi << 8)));
    }
    return pcm;
}

static void write_pcm16(const std::string& path, const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> bytes;
    bytes.reserve(pcm.size() * 2);

    for (int16_t s : pcm) {
        uint16_t u = uint16_t(s);
        bytes.push_back(uint8_t(u & 0xFF));
        bytes.push_back(uint8_t((u >> 8) & 0xFF));
    }
    write_file(path, bytes);
}

static std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);

    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back((b >> i) & 1);
        }
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

static std::vector<uint8_t> repetition_encode_2x(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out;
    out.reserve(bits.size() * 2);
    for (uint8_t b : bits) {
        out.push_back(b);
        out.push_back(b);
    }
    return out;
}

static std::vector<uint8_t> repetition_decode_2x(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out;
    out.reserve(bits.size() / 2);

    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        out.push_back((bits[i] + bits[i + 1]) >= 1 ? 1 : 0);
    }
    return out;
}

/*
  Prosty interleaver blokowy.
  Wpisujemy wierszami, czytamy kolumnami.

  Przykład:
    input:  A B C D E F G H I J K L
    rows=3, cols=4

    A B C D
    E F G H
    I J K L

    output: A E I B F J C G K D H L
*/
static std::vector<uint8_t> interleave(const std::vector<uint8_t>& in, int rows = 8) {
    int cols = int((in.size() + rows - 1) / rows);
    std::vector<uint8_t> matrix(rows * cols, 0);

    for (size_t i = 0; i < in.size(); ++i) matrix[i] = in[i];

    std::vector<uint8_t> out;
    out.reserve(matrix.size());

    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            out.push_back(matrix[r * cols + c]);
        }
    }
    return out;
}

static std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& in, int rows = 8) {
    int cols = int((in.size() + rows - 1) / rows);
    std::vector<uint8_t> matrix(rows * cols, 0);

    size_t k = 0;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            if (k < in.size()) matrix[r * cols + c] = in[k++];
        }
    }

    return matrix;
}

static std::vector<double> make_p4_phase(int shift) {
    std::vector<double> phase(SYMBOL_SAMPLES);

    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        int idx = (n + shift) % SYMBOL_SAMPLES;
        phase[n] = M_PI * double(idx * idx) / double(SYMBOL_SAMPLES);
    }
    return phase;
}

static std::vector<double> make_symbol_wave(int symbol) {
    /*
      Używamy 8 cyklicznych przesunięć P4.
      Shifty są rozstrzelone po całej długości kodu, aby zwiększyć separację.
    */
    int shift = (symbol * SYMBOL_SAMPLES) / ALPHABET;
    auto phase = make_p4_phase(shift);

    std::vector<double> wave(SYMBOL_SAMPLES);
    for (int n = 0; n < SYMBOL_SAMPLES; ++n) {
        double t = double(n) / SAMPLE_RATE;
        double carrier = 2.0 * M_PI * CARRIER_HZ * t;
        wave[n] = AMP * std::cos(carrier + phase[n]);
    }
    return wave;
}

static std::vector<std::vector<double>> make_templates() {
    std::vector<std::vector<double>> t;
    for (int s = 0; s < ALPHABET; ++s) t.push_back(make_symbol_wave(s));
    return t;
}

static void append_symbol_pcm(std::vector<int16_t>& pcm, int symbol) {
    static const auto templates = make_templates();
    const auto& w = templates[symbol & 7];

    for (double x : w) {
        int v = int(std::round(x * 32767.0));
        v = std::clamp(v, -32768, 32767);
        pcm.push_back(int16_t(v));
    }
}

static double corr_score(const int16_t* samples, const std::vector<double>& tpl) {
    /*
      Korelacja z usunięciem DC i normalizacją energii.
      To pomaga przy różnym poziomie audio.
    */
    double mean = 0.0;
    for (int i = 0; i < SYMBOL_SAMPLES; ++i) mean += samples[i];
    mean /= SYMBOL_SAMPLES;

    double dot = 0.0;
    double e1 = 0.0;
    double e2 = 0.0;

    for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
        double a = double(samples[i]) - mean;
        double b = tpl[i] * 32767.0;
        dot += a * b;
        e1 += a * a;
        e2 += b * b;
    }

    if (e1 <= 1e-9 || e2 <= 1e-9) return 0.0;
    return std::abs(dot) / std::sqrt(e1 * e2);
}

static int decode_symbol_at(const std::vector<int16_t>& pcm, size_t pos, double* best_out = nullptr) {
    static const auto templates = make_templates();

    int best = 0;
    double best_score = -1.0;

    for (int s = 0; s < ALPHABET; ++s) {
        double sc = corr_score(&pcm[pos], templates[s]);
        if (sc > best_score) {
            best_score = sc;
            best = s;
        }
    }

    if (best_out) *best_out = best_score;
    return best;
}

static std::vector<int> bits_to_symbols(const std::vector<uint8_t>& bits) {
    std::vector<int> symbols;

    for (size_t i = 0; i < bits.size(); i += 3) {
        int b0 = i < bits.size() ? bits[i] : 0;
        int b1 = i + 1 < bits.size() ? bits[i + 1] : 0;
        int b2 = i + 2 < bits.size() ? bits[i + 2] : 0;
        symbols.push_back((b0 << 2) | (b1 << 1) | b2);
    }
    return symbols;
}

static void encode_file(const std::string& in_path, const std::string& out_pcm_path) {
    auto payload = read_file(in_path);
    if (payload.size() > 4096) {
        throw std::runtime_error("Prototype limit: input max 4096 bytes");
    }

    std::vector<uint8_t> frame;
    frame.push_back(uint8_t(payload.size() & 0xFF));
    frame.push_back(uint8_t((payload.size() >> 8) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());

    uint16_t crc = crc16_ccitt(frame);
    frame.push_back(uint8_t(crc & 0xFF));
    frame.push_back(uint8_t((crc >> 8) & 0xFF));

    auto bits = bytes_to_bits(frame);
    auto fec = repetition_encode_2x(bits);
    auto ilv = interleave(fec, 8);
    auto symbols = bits_to_symbols(ilv);

    std::vector<int16_t> pcm;

    /*
      Preambuła:
        powtarzamy symbol 0, żeby VOX i AGC miały czas.
    */
    for (int i = 0; i < PREAMBLE_SYMBOLS; ++i) append_symbol_pcm(pcm, 0);

    /*
      SYNC:
        znana sekwencja symboli o dobrej zmienności.
    */
    const int sync_seq[SYNC_SYMBOLS] = {
        7, 1, 6, 2, 5, 3, 4, 0,
        7, 2, 6, 1, 5, 0, 4, 3
    };

    for (int s : sync_seq) append_symbol_pcm(pcm, s);

    /*
      Dane.
    */
    for (int s : symbols) append_symbol_pcm(pcm, s);

    /*
      Krótki ogon ciszy.
    */
    pcm.insert(pcm.end(), SAMPLE_RATE / 2, 0);

    write_pcm16(out_pcm_path, pcm);

    std::cerr << "Encoded " << payload.size() << " bytes into "
              << pcm.size() << " PCM samples, duration "
              << double(pcm.size()) / SAMPLE_RATE << " s\n";
}

static int find_sync(const std::vector<int16_t>& pcm) {
    const int sync_seq[SYNC_SYMBOLS] = {
        7, 1, 6, 2, 5, 3, 4, 0,
        7, 2, 6, 1, 5, 0, 4, 3
    };

    int total_symbols = int(pcm.size() / SYMBOL_SAMPLES);
    int best_pos = -1;
    int best_hits = -1;

    /*
      Prosty skaner symbolowy.
      Zakładamy, że początek jest mniej więcej wyrównany do SYMBOL_SAMPLES.
      Dla prawdziwego audio warto skanować z krokiem np. 4 próbki i policzyć
      korelację całego SYNC, a nie tylko twardo zdekodowane symbole.
    */
    for (int sym = 0; sym + SYNC_SYMBOLS < total_symbols; ++sym) {
        int hits = 0;
        for (int j = 0; j < SYNC_SYMBOLS; ++j) {
            size_t pos = size_t(sym + j) * SYMBOL_SAMPLES;
            double score = 0.0;
            int ds = decode_symbol_at(pcm, pos, &score);
            if (ds == sync_seq[j]) hits++;
        }

        if (hits > best_hits) {
            best_hits = hits;
            best_pos = sym;
        }
    }

    if (best_hits < 12) {
        std::cerr << "Warning: weak sync, hits=" << best_hits << "/" << SYNC_SYMBOLS << "\n";
    } else {
        std::cerr << "Sync hits=" << best_hits << "/" << SYNC_SYMBOLS << "\n";
    }

    return best_pos;
}

static void decode_file(const std::string& in_pcm_path, const std::string& out_path) {
    auto pcm = read_pcm16(in_pcm_path);
    if (pcm.size() < size_t((PREAMBLE_SYMBOLS + SYNC_SYMBOLS) * SYMBOL_SAMPLES)) {
        throw std::runtime_error("PCM too short");
    }

    int sync_sym = find_sync(pcm);
    if (sync_sym < 0) throw std::runtime_error("Sync not found");

    int data_start_sym = sync_sym + SYNC_SYMBOLS;
    size_t pos = size_t(data_start_sym) * SYMBOL_SAMPLES;

    std::vector<int> symbols;
    while (pos + SYMBOL_SAMPLES <= pcm.size()) {
        double score = 0.0;
        int s = decode_symbol_at(pcm, pos, &score);
        symbols.push_back(s);
        pos += SYMBOL_SAMPLES;
    }

    std::vector<uint8_t> symbol_bits;
    symbol_bits.reserve(symbols.size() * BITS_PER_SYMBOL);
    for (int s : symbols) {
        symbol_bits.push_back((s >> 2) & 1);
        symbol_bits.push_back((s >> 1) & 1);
        symbol_bits.push_back(s & 1);
    }

    std::vector<uint8_t> payload;
    bool found = false;

    for (size_t symbol_count = 1; symbol_count <= symbols.size(); ++symbol_count) {
        size_t bit_count = symbol_count * BITS_PER_SYMBOL;
        size_t byte_aligned = (bit_count / 8) * 8;
        if (byte_aligned < 8) continue;

        std::vector<uint8_t> ilv_bits(symbol_bits.begin(), symbol_bits.begin() + byte_aligned);
        auto fec_bits = deinterleave(ilv_bits, 8);
        auto data_bits = repetition_decode_2x(fec_bits);
        auto bytes = bits_to_bytes(data_bits);

        if (bytes.size() < 4) continue;

        uint16_t len = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
        if (len > 4096) continue;

        size_t frame_len = 2 + size_t(len) + 2;
        if (bytes.size() < frame_len) continue;

        std::vector<uint8_t> frame(bytes.begin(), bytes.begin() + frame_len - 2);
        uint16_t got_crc = uint16_t(bytes[frame_len - 2]) |
                           (uint16_t(bytes[frame_len - 1]) << 8);
        uint16_t calc_crc = crc16_ccitt(frame);

        if (got_crc == calc_crc) {
            payload.assign(bytes.begin() + 2, bytes.begin() + 2 + len);
            found = true;
            break;
        }
    }

    if (!found) {
        throw std::runtime_error("CRC check failed or valid frame not found");
    }

    write_file(out_path, payload);

    std::cerr << "Decoded " << payload.size() << " bytes OK\n";
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr <<
                "Usage:\n"
                "  " << argv[0] << " enc input.bin output.pcm\n"
                "  " << argv[0] << " dec input.pcm output.bin\n";
            return 1;
        }

        std::string mode = argv[1];

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