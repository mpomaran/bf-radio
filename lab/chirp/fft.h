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

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base);

double cyclic_corr_sample(const std::array<double, config::SYMBOL_SAMPLES>& corr,
                          double idx);

}  // namespace dsp
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_FFT_H_
