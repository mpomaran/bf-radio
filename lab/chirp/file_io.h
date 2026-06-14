// file_io.h
//
// Binary file IO helpers. This module owns only whole-file byte reads/writes
// and has no modem, PCM, DSP, or protocol knowledge.

#ifndef BF_RADIO_LAB_CHIRP_FILE_IO_H_
#define BF_RADIO_LAB_CHIRP_FILE_IO_H_

#include <cstdint>
#include <string>
#include <vector>

namespace chirp {
namespace io {

std::vector<uint8_t> read_file(const std::string& path);
void write_file(const std::string& path, const std::vector<uint8_t>& data);

}  // namespace io
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_FILE_IO_H_
