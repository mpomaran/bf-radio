#include "lab/chirp/crc16.h"

namespace chirp {
namespace frame {

uint16_t crc16_ccitt(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= uint16_t(b) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
        }
    }
    return crc;
}

}  // namespace frame
}  // namespace chirp
