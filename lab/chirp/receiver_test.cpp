#include "lab/chirp/receiver.h"

#include <cassert>
#include <cstdint>
#include <vector>

#include "lab/chirp/modulator.h"

int main() {
    std::vector<uint8_t> payload;
    for (int i = 0; i < 48; ++i) payload.push_back(uint8_t((i * 19 + 7) & 0xFF));

    const std::vector<int16_t> pcm = chirp::modulator::encode_payload_to_pcm(payload);

    chirp::receiver::ReceiverOptions robust;
    const chirp::receiver::DecodeResult robust_result =
        chirp::receiver::decode_payload_from_pcm(pcm, robust);
    assert(robust_result.ok);
    assert(robust_result.payload == payload);
    assert(robust_result.diagnostics.ok);

    chirp::receiver::ReceiverOptions legacy;
    chirp::receiver::apply_receiver_profile_defaults(
        &legacy, chirp::receiver::ReceiverProfile::Legacy);
    const chirp::receiver::DecodeResult legacy_result =
        chirp::receiver::decode_payload_from_pcm(pcm, legacy);
    assert(legacy_result.ok);
    assert(legacy_result.payload == payload);
    assert(legacy_result.diagnostics.ok);
    return 0;
}
