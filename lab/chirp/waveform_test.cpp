#include "lab/chirp/waveform.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

int main() {
    const std::vector<double> base = chirp::waveform::make_base_chirp();
    assert(base.size() == size_t(chirp::config::SYMBOL_SAMPLES));

    const std::vector<double> symbol0 = chirp::waveform::make_symbol_wave(0.0);
    const std::vector<double>& tpl0 = chirp::waveform::symbol_template(0, 1);
    assert(symbol0.size() == tpl0.size());
    for (size_t i = 0; i < symbol0.size(); ++i) {
        assert(std::abs(symbol0[i] - tpl0[i]) < 1e-12);
    }

    const std::array<double, chirp::config::SYMBOL_SAMPLES>& ideal =
        chirp::waveform::ideal_base_template_array();
    double energy = 0.0;
    for (double v : ideal) energy += v * v;
    assert(std::abs(energy - 1.0) < 1e-9);

    std::vector<int16_t> pcm;
    chirp::waveform::append_symbol_pcm(pcm, 3);
    assert(pcm.size() == size_t(chirp::config::SYMBOL_SAMPLES));
    return 0;
}
