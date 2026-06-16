#include "lab/chirp/sample_view.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lab/chirp/config.h"

namespace {

void require_close(double actual, double expected, double tolerance) {
    assert(std::abs(actual - expected) <= tolerance);
}

}  // namespace

int main() {
    using chirp::config::SYMBOL_SAMPLES;

    {
        const std::vector<int16_t> pcm = {0, 10, 20};
        require_close(chirp::sample::sample_at(pcm, 0.5), 5.0, 1e-12);
        require_close(chirp::sample::sample_at(pcm, -4.0), 0.0, 1e-12);
        require_close(chirp::sample::sample_at(pcm, 99.0), 20.0, 1e-12);
    }
    {
        std::array<double, SYMBOL_SAMPLES> samples = {};
        samples.fill(2.0);
        chirp::sample::normalize_template(&samples);
        for (double v : samples) require_close(v, 0.0, 1e-12);
    }
    {
        std::array<double, SYMBOL_SAMPLES> samples = {};
        for (int i = 0; i < SYMBOL_SAMPLES; ++i) {
            samples[size_t(i)] = double(i);
        }
        require_close(chirp::sample::cyclic_array_sample(samples, 1.25), 1.25, 1e-12);
        require_close(chirp::sample::cyclic_array_sample(samples, -0.5),
                      (double(SYMBOL_SAMPLES - 1) + 0.0) * 0.5, 1e-12);
    }
    {
        std::vector<int16_t> pcm(size_t(SYMBOL_SAMPLES + 2));
        for (size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = int16_t((int(i) % 17) * 100);
        }
        const std::array<double, SYMBOL_SAMPLES> normalized =
            chirp::sample::normalized_symbol_samples(pcm, 0.0, double(SYMBOL_SAMPLES));
        double mean = 0.0;
        double energy = 0.0;
        for (double v : normalized) {
            mean += v;
            energy += v * v;
        }
        mean /= SYMBOL_SAMPLES;
        require_close(mean, 0.0, 1e-12);
        require_close(std::sqrt(energy / SYMBOL_SAMPLES), 1.0, 1e-12);
    }
    return 0;
}
