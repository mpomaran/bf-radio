#ifndef BF_RADIO_SRC_RADIO_DEVICES_H_
#define BF_RADIO_SRC_RADIO_DEVICES_H_

#include <string>
#include <vector>

namespace radio {

struct DeviceCandidate {
    std::string id;
    std::string name;
    bool likely_digirig = false;
};

std::vector<DeviceCandidate> list_serial_devices();
std::vector<DeviceCandidate> list_playback_devices();
std::vector<DeviceCandidate> list_recording_devices();
int choose_candidate(const std::vector<DeviceCandidate>& devices,
                     const std::string& requested,
                     bool interactive);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_DEVICES_H_
