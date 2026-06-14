#include "lab/chirp/fft.h"

#include <cassert>
#include <cmath>
#include <complex>

int main() {
    std::array<std::complex<double>, chirp::config::SYMBOL_SAMPLES> x = {};
    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        x[size_t(i)] = std::complex<double>(std::sin(0.1 * i), std::cos(0.07 * i));
    }
    const auto original = x;
    chirp::dsp::fft128(&x, false);
    chirp::dsp::fft128(&x, true);
    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        assert(std::abs(x[size_t(i)].real() - original[size_t(i)].real()) < 1e-9);
        assert(std::abs(x[size_t(i)].imag() - original[size_t(i)].imag()) < 1e-9);
    }

    std::array<double, chirp::config::SYMBOL_SAMPLES> a = {};
    std::array<double, chirp::config::SYMBOL_SAMPLES> b = {};
    a[0] = 1.0;
    b[0] = 1.0;
    const auto corr = chirp::dsp::circular_chirp_correlation(a, b);
    assert(corr[0] > 0.99);
    for (int i = 1; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        assert(std::abs(corr[size_t(i)]) < 1e-9);
    }
    return 0;
}
