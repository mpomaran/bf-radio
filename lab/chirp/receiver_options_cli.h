// receiver_options_cli.h
//
// Command-line parsing for receiver-specific global flags. Parsing returns the
// remaining command arguments unchanged so the CLI frontend can keep its
// command dispatch behavior.

#ifndef BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_CLI_H_
#define BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_CLI_H_

#include <string>
#include <vector>

#include "lab/chirp/receiver_options.h"

namespace chirp {
namespace receiver {

ReceiverOptions parse_receiver_options_from_cli(
    int argc,
    char** argv,
    std::vector<std::string>* remaining_args);

}  // namespace receiver
}  // namespace chirp

#endif  // BF_RADIO_LAB_CHIRP_RECEIVER_OPTIONS_CLI_H_
