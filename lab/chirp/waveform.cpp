#include "lab/chirp/waveform.h"

#include <algorithm>
#include <cmath>

namespace chirp {
namespace waveform {

std::vector<double> make_base_chirp() {
    std::vector<double> chirp(config::SYMBOL_SAMPLES);
    const double duration = double(config::SYMBOL_SAMPLES) / config::SAMPLE_RATE;
    const double sweep = (config::FREQ_HIGH - config::FREQ_LOW) / duration;

    for (int n = 0; n < config::SYMBOL_SAMPLES; ++n) {
        const double t = double(n) / config::SAMPLE_RATE;
        const double phase =
            2.0 * config::PI * (config::FREQ_LOW * t + 0.5 * sweep * t * t);
        chirp[size_t(n)] = config::AMP * std::cos(phase);
    }
    return chirp;
}

const std::array<double, config::SYMBOL_SAMPLES>& ideal_base_template_array() {
    static const std::array<double, config::SYMBOL_SAMPLES> base = [] {
        const std::vector<double> chirp = make_base_chirp();
        std::array<double, config::SYMBOL_SAMPLES> out = {};
        for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) out[size_t(i)] = chirp[size_t(i)];
        double energy = 0.0;
        for (double v : out) energy += v * v;
        if (energy > 1e-12) {
            const double inv = 1.0 / std::sqrt(energy);
            for (double& v : out) v *= inv;
        }
        return out;
    }();
    return base;
}

double cyclic_sample(const std::vector<double>& wave, double idx) {
    const double n = double(wave.size());
    idx = std::fmod(idx, n);
    if (idx < 0.0) idx += n;
    const int i0 = int(std::floor(idx));
    const int i1 = (i0 + 1) % int(wave.size());
    const double frac = idx - i0;
    return wave[size_t(i0)] + (wave[size_t(i1)] - wave[size_t(i0)]) * frac;
}

std::vector<double> make_symbol_wave(double symbol) {
    static const std::vector<double> base = make_base_chirp();
    const double shift = symbol * double(config::SYMBOL_SAMPLES) / config::ALPHABET;
    std::vector<double> out(config::SYMBOL_SAMPLES);
    for (int n = 0; n < config::SYMBOL_SAMPLES; ++n) {
        out[size_t(n)] = cyclic_sample(base, double(n) + shift);
    }
    return out;
}

const std::vector<double>& symbol_template(int symbol, int offset_index) {
    static const double offsets[] = {-0.25, 0.0, 0.25};
    static const std::vector<std::vector<double> > templates = [] {
        std::vector<std::vector<double> > out;
        out.reserve(config::ALPHABET * 3);
        for (int symbol = 0; symbol < config::ALPHABET; ++symbol) {
            for (double offset : offsets) out.push_back(make_symbol_wave(double(symbol) + offset));
        }
        return out;
    }();
    return templates[size_t(symbol * 3 + offset_index)];
}

void append_symbol_pcm(std::vector<int16_t>& pcm, int raw_symbol) {
    const std::vector<double> wave = make_symbol_wave(double(raw_symbol & 0x0F));
    for (double x : wave) {
        int v = int(std::round(x * 32767.0));
        v = std::max(-32768, std::min(32767, v));
        pcm.push_back(int16_t(v));
    }
}

}  // namespace waveform
}  // namespace chirp
