#include "lab/chirp/crc16.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    const std::vector<uint8_t> known = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(chirp::frame::crc16_ccitt(known) == 0x29B1);

    std::vector<uint8_t> changed = known;
    changed[3] ^= 0x01;
    assert(chirp::frame::crc16_ccitt(changed) != chirp::frame::crc16_ccitt(known));
    return 0;
}
