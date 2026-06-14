// crc16.h
//
// CRC16-CCITT helper used by protected modem frames. This module owns only the
// checksum primitive and does not parse frames or perform IO.

#ifndef BF_RADIO_LAB_CHIRP_CRC16_H_
#define BF_RADIO_LAB_CHIRP_CRC16_H_

#include <cstdint>
#include <vector>

namespace chirp {
namespace frame {

uint16_t crc16_ccitt(const std::vector<uint8_t>& data);

}  // namespace frame
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_CRC16_H_
