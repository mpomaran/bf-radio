// fft.h
//
// Fixed-size FFT and circular correlation support for chirp symbol metrics.
// This module owns only the 128-sample DSP primitive and passes arrays by
// reference to avoid heap allocation in hot receive-path helpers.

#ifndef BF_RADIO_LAB_CHIRP_FFT_H_
#define BF_RADIO_LAB_CHIRP_FFT_H_

#include <array>
#include <complex>

#include "lab/chirp/config.h"

namespace chirp {
namespace dsp {

void fft128(std::array<std::complex<double>, config::SYMBOL_SAMPLES>* a, bool inverse);

struct PrecomputedChirpTemplate {
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> fft;

    PrecomputedChirpTemplate() : fft() {}
};

struct CircularCorrelationScratch {
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> samples_fft;

    CircularCorrelationScratch() {}
};

struct FftCorrelationDiagnostics {
    unsigned long long base_fft_cache_hits;
    unsigned long long base_fft_cache_misses;
    unsigned long long sample_ffts_computed;
    int base_fft_cache_entries;
    int base_fft_cache_capacity;

    FftCorrelationDiagnostics()
        : base_fft_cache_hits(0),
          base_fft_cache_misses(0),
          sample_ffts_computed(0),
          base_fft_cache_entries(0),
          base_fft_cache_capacity(0) {}
};

void reset_circular_chirp_correlation_diagnostics();
FftCorrelationDiagnostics circular_chirp_correlation_diagnostics();

PrecomputedChirpTemplate make_precomputed_chirp_template(
    const std::array<double, config::SYMBOL_SAMPLES>& base);

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation_precomputed(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const PrecomputedChirpTemplate& base,
    CircularCorrelationScratch* scratch);

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base);

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation_uncached(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base);

double cyclic_corr_sample(const std::array<double, config::SYMBOL_SAMPLES>& corr,
                          double idx);

}  // namespace dsp
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_FFT_H_
