#include "lab/chirp/receiver_diagnostics.h"

#include <cassert>
#include <string>

int main() {
    chirp::receiver::TimingDiagnostics timing;
    assert(timing.clock_scale == 1.0);
    assert(timing.pilot_count == 0);
    assert(timing.span_estimate_mean == chirp::config::NOMINAL_SPAN);

    chirp::receiver::DecodeAttemptDiagnostics attempt;
    assert(!attempt.ok);
    assert(attempt.cause == chirp::receiver::DecodeFailureCause::Sync);
    assert(attempt.weight_fallback_used);

    chirp::receiver::ReceiverDiagnostics rx;
    assert(rx.fec_enabled);
    assert(rx.fec_mode != nullptr);

    assert(chirp::receiver::decode_failure_cause_name(
               chirp::receiver::DecodeFailureCause::None) == std::string("none"));
    assert(chirp::receiver::decode_failure_cause_name(
               chirp::receiver::DecodeFailureCause::HeaderFec) ==
           std::string("header_fec"));
    return 0;
}
