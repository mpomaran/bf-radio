// pcm_io.h
//
// PCM16 little-endian file conversion helpers. This module converts between raw
// bytes and signed 16-bit PCM samples. It does not know about modem framing,
// symbols, chirps, or FEC.

#ifndef BF_RADIO_LAB_CHIRP_PCM_IO_H_
#define BF_RADIO_LAB_CHIRP_PCM_IO_H_

#include <cstdint>
#include <string>
#include <vector>

namespace chirp {
namespace io {

std::vector<int16_t> read_pcm16(const std::string& path);
void write_pcm16(const std::string& path, const std::vector<int16_t>& pcm);

}  // namespace io
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_PCM_IO_H_
