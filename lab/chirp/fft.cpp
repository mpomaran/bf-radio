#include "lab/chirp/fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace chirp {
namespace dsp {
namespace {

constexpr int kBaseFftCacheCapacity = 8;

struct BaseFftCacheEntry {
    bool valid;
    uint64_t hash;
    unsigned long long last_used;
    std::array<double, config::SYMBOL_SAMPLES> base;
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> fft;

    BaseFftCacheEntry()
        : valid(false), hash(0), last_used(0), base(), fft() {}
};

struct BaseFftCache {
    std::array<BaseFftCacheEntry, kBaseFftCacheCapacity> entries;
    FftCorrelationDiagnostics diagnostics;
    unsigned long long clock;

    BaseFftCache() : entries(), diagnostics(), clock(0) {
        diagnostics.base_fft_cache_capacity = kBaseFftCacheCapacity;
    }
};

// The receive path is currently single-threaded. Keeping this cache thread-local
// avoids cross-thread synchronization and keeps repeated-template acceleration
// deterministic for normal tests and command-line decoding.
thread_local BaseFftCache g_base_fft_cache;

uint64_t double_bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint64_t hash_base(const std::array<double, config::SYMBOL_SAMPLES>& base) {
    uint64_t hash = 1469598103934665603ULL;
    for (double value : base) {
        uint64_t bits = double_bits(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= uint8_t(bits & 0xFFU);
            hash *= 1099511628211ULL;
            bits >>= 8;
        }
    }
    return hash;
}

bool base_bits_equal(const std::array<double, config::SYMBOL_SAMPLES>& a,
                     const std::array<double, config::SYMBOL_SAMPLES>& b) {
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        if (double_bits(a[size_t(i)]) != double_bits(b[size_t(i)])) return false;
    }
    return true;
}

int cache_entry_count(const BaseFftCache& cache) {
    int count = 0;
    for (const BaseFftCacheEntry& entry : cache.entries) {
        if (entry.valid) ++count;
    }
    return count;
}

void fill_base_fft(const std::array<double, config::SYMBOL_SAMPLES>& base,
                   std::array<std::complex<double>, config::SYMBOL_SAMPLES>* out) {
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        (*out)[size_t(i)] = std::complex<double>(base[size_t(i)], 0.0);
    }
    fft128(out, false);
    ++g_base_fft_cache.diagnostics.base_ffts_computed;
}

const std::array<std::complex<double>, config::SYMBOL_SAMPLES>& cached_base_fft(
    const std::array<double, config::SYMBOL_SAMPLES>& base) {
    BaseFftCache& cache = g_base_fft_cache;
    const uint64_t hash = hash_base(base);
    ++cache.clock;

    for (BaseFftCacheEntry& entry : cache.entries) {
        if (entry.valid && entry.hash == hash && base_bits_equal(entry.base, base)) {
            entry.last_used = cache.clock;
            ++cache.diagnostics.base_fft_cache_hits;
            cache.diagnostics.base_fft_cache_entries = cache_entry_count(cache);
            return entry.fft;
        }
    }

    BaseFftCacheEntry* victim = &cache.entries[0];
    for (BaseFftCacheEntry& entry : cache.entries) {
        if (!entry.valid) {
            victim = &entry;
            break;
        }
        if (entry.last_used < victim->last_used) victim = &entry;
    }

    victim->valid = true;
    victim->hash = hash;
    victim->last_used = cache.clock;
    victim->base = base;
    fill_base_fft(base, &victim->fft);
    ++cache.diagnostics.base_fft_cache_misses;
    cache.diagnostics.base_fft_cache_entries = cache_entry_count(cache);
    return victim->fft;
}

void circular_chirp_correlation_with_base_fft_into(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<std::complex<double>, config::SYMBOL_SAMPLES>& base_fft,
    CircularCorrelationScratch* scratch,
    std::array<double, config::SYMBOL_SAMPLES>* out) {
    CircularCorrelationScratch local_scratch;
    CircularCorrelationScratch* work = scratch != nullptr ? scratch : &local_scratch;
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        work->samples_fft[size_t(i)] = std::complex<double>(samples[size_t(i)], 0.0);
    }

    fft128(&work->samples_fft, false);
    ++g_base_fft_cache.diagnostics.sample_ffts_computed;
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        work->samples_fft[size_t(i)] =
            std::conj(work->samples_fft[size_t(i)]) * base_fft[size_t(i)];
    }
    fft128(&work->samples_fft, true);

    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        (*out)[size_t(i)] = work->samples_fft[size_t(i)].real();
    }
}

}  // namespace

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

void reset_circular_chirp_correlation_diagnostics() {
    g_base_fft_cache = BaseFftCache();
}

FftCorrelationDiagnostics circular_chirp_correlation_diagnostics() {
    g_base_fft_cache.diagnostics.base_fft_cache_entries =
        cache_entry_count(g_base_fft_cache);
    g_base_fft_cache.diagnostics.base_fft_cache_capacity = kBaseFftCacheCapacity;
    return g_base_fft_cache.diagnostics;
}

PrecomputedChirpTemplate make_precomputed_chirp_template(
    const std::array<double, config::SYMBOL_SAMPLES>& base) {
    PrecomputedChirpTemplate out;
    fill_base_fft(base, &out.fft);
    return out;
}

void circular_chirp_correlation_precomputed_into(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const PrecomputedChirpTemplate& base,
    CircularCorrelationScratch* scratch,
    std::array<double, config::SYMBOL_SAMPLES>* out) {
    ++g_base_fft_cache.diagnostics.correlation_calls;
    ++g_base_fft_cache.diagnostics.precomputed_correlation_calls;
    circular_chirp_correlation_with_base_fft_into(samples, base.fft, scratch, out);
}

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation_precomputed(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const PrecomputedChirpTemplate& base,
    CircularCorrelationScratch* scratch) {
    std::array<double, config::SYMBOL_SAMPLES> out = {};
    circular_chirp_correlation_precomputed_into(samples, base, scratch, &out);
    return out;
}

void circular_chirp_correlation_into(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base,
    CircularCorrelationScratch* scratch,
    std::array<double, config::SYMBOL_SAMPLES>* out) {
    ++g_base_fft_cache.diagnostics.correlation_calls;
    circular_chirp_correlation_with_base_fft_into(samples, cached_base_fft(base), scratch, out);
}

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base) {
    std::array<double, config::SYMBOL_SAMPLES> out = {};
    circular_chirp_correlation_into(samples, base, nullptr, &out);
    return out;
}

void circular_chirp_correlation_uncached_into(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base,
    std::array<double, config::SYMBOL_SAMPLES>* out) {
    ++g_base_fft_cache.diagnostics.correlation_calls;
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> x = {};
    std::array<std::complex<double>, config::SYMBOL_SAMPLES> y = {};
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        x[size_t(i)] = std::complex<double>(samples[size_t(i)], 0.0);
        y[size_t(i)] = std::complex<double>(base[size_t(i)], 0.0);
    }

    fft128(&x, false);
    ++g_base_fft_cache.diagnostics.sample_ffts_computed;
    fft128(&y, false);
    ++g_base_fft_cache.diagnostics.base_ffts_computed;
    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        x[size_t(i)] = std::conj(x[size_t(i)]) * y[size_t(i)];
    }
    fft128(&x, true);

    for (int i = 0; i < config::SYMBOL_SAMPLES; ++i) {
        (*out)[size_t(i)] = x[size_t(i)].real();
    }
}

std::array<double, config::SYMBOL_SAMPLES> circular_chirp_correlation_uncached(
    const std::array<double, config::SYMBOL_SAMPLES>& samples,
    const std::array<double, config::SYMBOL_SAMPLES>& base) {
    std::array<double, config::SYMBOL_SAMPLES> out = {};
    circular_chirp_correlation_uncached_into(samples, base, &out);
    return out;
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
