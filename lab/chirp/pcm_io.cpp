#include "lab/chirp/pcm_io.h"

#include "lab/chirp/file_io.h"

namespace chirp {
namespace io {

std::vector<int16_t> read_pcm16(const std::string& path) {
    auto bytes = read_file(path);
    if (bytes.size() % 2) bytes.pop_back();

    std::vector<int16_t> pcm;
    pcm.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2) {
        pcm.push_back(int16_t(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8)));
    }
    return pcm;
}

void write_pcm16(const std::string& path, const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> bytes;
    bytes.reserve(pcm.size() * 2);
    for (int16_t s : pcm) {
        bytes.push_back(uint8_t(uint16_t(s) & 0xFF));
        bytes.push_back(uint8_t((uint16_t(s) >> 8) & 0xFF));
    }
    write_file(path, bytes);
}

}  // namespace io
}  // namespace chirp
