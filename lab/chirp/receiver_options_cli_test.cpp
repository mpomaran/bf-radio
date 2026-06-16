#include "lab/chirp/receiver_options_cli.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<char*> argv_from(std::vector<std::string>* args) {
    std::vector<char*> argv;
    argv.reserve(args->size());
    for (std::string& arg : *args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return argv;
}

chirp::receiver::ReceiverOptions parse_args(std::vector<std::string> args,
                                            std::vector<std::string>* remaining) {
    std::vector<char*> argv = argv_from(&args);
    return chirp::receiver::parse_receiver_options_from_cli(
        static_cast<int>(argv.size()), argv.data(), remaining);
}

}  // namespace

int main() {
    {
        std::vector<std::string> remaining;
        const chirp::receiver::ReceiverOptions options =
            parse_args({"chirp_modem", "dec", "in.pcm", "out.bin"}, &remaining);
        assert(options.receiver_profile == chirp::receiver::ReceiverProfile::Robust);
        assert(options.timing_search_profile ==
               chirp::receiver::TimingSearchProfile::Local);
        assert(options.adaptive_llr_enabled);
        assert(options.adaptive_clock_tracking_enabled);
        assert(options.adaptive_channel_templates_enabled);
        assert(options.weighted_correlation_enabled);
        assert(remaining.size() == 3);
        assert(remaining[0] == "dec");
        assert(remaining[1] == "in.pcm");
        assert(remaining[2] == "out.bin");
    }
    {
        std::vector<std::string> remaining;
        const chirp::receiver::ReceiverOptions options =
            parse_args({"chirp_modem", "--legacy-receiver", "--timing-search=full",
                        "--adaptive-llr", "--llr-scale", "0.75", "measure-pcm", "1"},
                       &remaining);
        assert(options.receiver_profile == chirp::receiver::ReceiverProfile::Legacy);
        assert(options.timing_search_profile ==
               chirp::receiver::TimingSearchProfile::Full);
        assert(options.adaptive_llr_enabled);
        assert(!options.adaptive_clock_tracking_enabled);
        assert(!options.adaptive_channel_templates_enabled);
        assert(!options.weighted_correlation_enabled);
        assert(options.manual_llr_scale_set);
        assert(options.manual_llr_scale == 0.75);
        assert(remaining.size() == 2);
        assert(remaining[0] == "measure-pcm");
        assert(remaining[1] == "1");
    }
    {
        std::vector<std::string> remaining;
        const chirp::receiver::ReceiverOptions options =
            parse_args({"chirp_modem", "--rx-profile", "robust", "--no-adaptive-llr",
                        "--no-weighted-correlation", "--rx-diagnostics", "selftest"},
                       &remaining);
        assert(options.receiver_profile == chirp::receiver::ReceiverProfile::Robust);
        assert(!options.adaptive_llr_enabled);
        assert(options.adaptive_clock_tracking_enabled);
        assert(options.adaptive_channel_templates_enabled);
        assert(!options.weighted_correlation_enabled);
        assert(options.rx_diagnostics_enabled);
        assert(remaining.size() == 1);
        assert(remaining[0] == "selftest");
    }
    {
        bool threw = false;
        try {
            std::vector<std::string> remaining;
            (void)parse_args({"chirp_modem", "--llr-scale", "0"}, &remaining);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw);
    }
    return 0;
}
