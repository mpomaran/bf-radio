#include "lab/chirp/fft.h"

#include <algorithm>
#include <cmath>

namespace chirp {
namespace dsp {

void fft128(std::array<std::complex<double>, config::SYMBOL_SAMPLES>* a, bool inverse) {
    int j = 0;
    for (int i = 1; i < config::SYMBOL_SAMPLES; ++i) {
        int bit = config::SYMBOL_SAMPLES >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) std::swap((*a)[size_t(i)], (*a)[size_t(j)]);
    }

    for (int len = 2; len <= config::SYMBOL_SAMPLES; len <<= 1) {
        const double angle = (inverse ? 2.0 : -2.0) * config::PI / double(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < config::SYMBOL_SAMPLES; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<double> u = (*a)[size_t(i + j)];
                const std::complex<double> v = (*a)[size_t(i + j + len / 2)] * w;
                (*a)[size_t(i + j)] = u + v;
                (*a)[size_t(i + j + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (std::complex<double>& v : *a) v /= double(config::SYMBOL_SAMPLES);
    }
}

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base) {
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> x = {};
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> y = {};
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        x[size_t(i)] = std::complex<double>(samples[size_t(i)], 0.0);
        y[size_t(i)] = std::complex<double>(base[size_t(i)], 0.0);
    }

    fft128(&x, false);
    fft128(&y, false);
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        x[size_t(i)] = std::conj(x[size_t(i)]) * y[size_t(i)];
    }
    fft128(&x, true);

    std::array<double, config::SYMBOL_SAMPLES> corr = {};
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        corr[size_t(i)] = x[size_t(i)].real();
    }
    return corr;
}

double cyclic_corr_sample(const std::array<double, config::SYMBOL_SAMPLES>& corr,
                          double idx) {
    idx = std::fmod(idx, double(config::SYMBOL_SAMPLES));
    if (idx < 0.0) idx += config::SYMBOL_SAMPLES;
    const int i0 = int(std::floor(idx));
    const int i1 = (i0 + 1) & (config::SYMBOL_SAMPLES - 1);
    const double frac = idx - double(i0);
    return corr[size_t(i0)] * (1.0 - frac) + corr[size_t(i1)] * frac;
}

}  // namespace dsp
}  // namespace chirp
