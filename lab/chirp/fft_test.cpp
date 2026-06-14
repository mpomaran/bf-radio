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

    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        a[size_t(i)] = std::sin(0.13 * i) + 0.2 * std::cos(0.31 * i);
        b[size_t(i)] = std::cos(0.17 * i) - 0.1 * std::sin(0.23 * i);
    }

    chirp::dsp::reset_circular_chirp_correlation_diagnostics();
    const auto uncached = chirp::dsp::circular_chirp_correlation_uncached(a, b);
    chirp::dsp::CircularCorrelationScratch scratch;
    const chirp::dsp::PrecomputedChirpTemplate precomputed =
        chirp::dsp::make_precomputed_chirp_template(b);
    const auto explicit_precomputed =
        chirp::dsp::circular_chirp_correlation_precomputed(a, precomputed, &scratch);
    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        assert(std::abs(uncached[size_t(i)] - explicit_precomputed[size_t(i)]) < 1e-10);
    }

    chirp::dsp::reset_circular_chirp_correlation_diagnostics();
    const auto cached1 = chirp::dsp::circular_chirp_correlation(a, b);
    const auto cached2 = chirp::dsp::circular_chirp_correlation(a, b);
    for (int i = 0; i < chirp::config::SYMBOL_SAMPLES; ++i) {
        assert(std::abs(uncached[size_t(i)] - cached1[size_t(i)]) < 1e-10);
        assert(std::abs(cached1[size_t(i)] - cached2[size_t(i)]) < 1e-12);
    }

    chirp::dsp::FftCorrelationDiagnostics diag =
        chirp::dsp::circular_chirp_correlation_diagnostics();
    assert(diag.base_fft_cache_misses == 1);
    assert(diag.base_fft_cache_hits == 1);
    assert(diag.sample_ffts_computed == 2);
    assert(diag.base_fft_cache_entries == 1);
    assert(diag.base_fft_cache_capacity >= 4);

    std::array<double, chirp::config::SYMBOL_SAMPLES> changed_base = b;
    changed_base[7] += 1e-6;
    chirp::dsp::circular_chirp_correlation(a, changed_base);
    diag = chirp::dsp::circular_chirp_correlation_diagnostics();
    assert(diag.base_fft_cache_misses == 2);
    assert(diag.base_fft_cache_hits == 1);

    for (int variant = 0; variant < diag.base_fft_cache_capacity + 3; ++variant) {
        std::array<double, chirp::config::SYMBOL_SAMPLES> base_variant = b;
        base_variant[0] += 0.001 * double(variant + 1);
        chirp::dsp::circular_chirp_correlation(a, base_variant);
    }
    diag = chirp::dsp::circular_chirp_correlation_diagnostics();
    assert(diag.base_fft_cache_entries <= diag.base_fft_cache_capacity);
    return 0;
}
