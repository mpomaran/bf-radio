// waveform.h
//
// Chirp waveform generation and symbol templates. This module owns the
// deterministic CSS symbol waveforms used by both the transmitter and the
// receiver metrics. It does not perform frame encoding or sync acquisition.

#ifndef BF_RADIO_LAB_CHIRP_WAVEFORM_H_
#define BF_RADIO_LAB_CHIRP_WAVEFORM_H_

#include <array>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

namespace chirp {
namespace waveform {

std::vector<double> make_base_chirp();
const std::array<double, config::SYMBOL_SAMPLES>& ideal_base_template_array();
double cyclic_sample(const std::vector<double>& wave, double idx);
std::vector<double> make_symbol_wave(double symbol);
const std::vector<double>& symbol_template(int symbol, int offset_index);
void append_symbol_pcm(std::vector<int16_t>& pcm, int raw_symbol);

}  // namespace waveform
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_WAVEFORM_H_
