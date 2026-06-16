#include "lab/chirp/receiver_options_cli.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace chirp {
namespace receiver {
namespace {

double parse_cli_double(const std::string& value, const std::string& name) {
    size_t parsed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &parsed);
    } catch (...) {
        throw std::runtime_error("Invalid number for " + name + ": " + value);
    }
    if (parsed != value.size()) {
        throw std::runtime_error("Invalid number for " + name + ": " + value);
    }
    return result;
}

}  // namespace

ReceiverOptions parse_receiver_options_from_cli(
    int argc,
    char** argv,
    std::vector<std::string>* remaining_args) {
    remaining_args->clear();
    remaining_args->reserve(size_t(std::max(0, argc - 1)));

    ReceiverOptions options;
    ReceiverProfile selected_profile = ReceiverProfile::Robust;
    bool has_timing_search_override = false;
    TimingSearchProfile timing_search_override = options.timing_search_profile;
    bool has_adaptive_llr_override = false;
    bool adaptive_llr_override = false;
    bool has_adaptive_clock_override = false;
    bool adaptive_clock_override = false;
    bool has_adaptive_templates_override = false;
    bool adaptive_templates_override = false;
    bool has_weighted_correlation_override = false;
    bool weighted_correlation_override = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string prefix = "--timing-search=";
        const std::string rx_profile_prefix = "--rx-profile=";
        const std::string llr_scale_prefix = "--llr-scale=";
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            timing_search_override = parse_timing_search_profile(arg.substr(prefix.size()));
            has_timing_search_override = true;
        } else if (arg == "--timing-search" && i + 1 < argc) {
            timing_search_override = parse_timing_search_profile(argv[++i]);
            has_timing_search_override = true;
        } else if (arg == "--timing-search") {
            throw std::runtime_error("--timing-search requires full, local, or center");
        } else if (arg.compare(0, rx_profile_prefix.size(), rx_profile_prefix) == 0) {
            selected_profile = parse_receiver_profile(arg.substr(rx_profile_prefix.size()));
        } else if (arg == "--rx-profile" && i + 1 < argc) {
            selected_profile = parse_receiver_profile(argv[++i]);
        } else if (arg == "--rx-profile") {
            throw std::runtime_error("--rx-profile requires legacy or robust");
        } else if (arg == "--legacy-receiver") {
            selected_profile = ReceiverProfile::Legacy;
        } else if (arg == "--robust-receiver") {
            selected_profile = ReceiverProfile::Robust;
        } else if (arg == "--rx-diagnostics" || arg == "--diagnostics") {
            options.rx_diagnostics_enabled = true;
        } else if (arg == "--adaptive-llr") {
            adaptive_llr_override = true;
            has_adaptive_llr_override = true;
        } else if (arg == "--adaptive-clock-tracking") {
            adaptive_clock_override = true;
            has_adaptive_clock_override = true;
        } else if (arg == "--no-adaptive-clock-tracking") {
            adaptive_clock_override = false;
            has_adaptive_clock_override = true;
        } else if (arg == "--adaptive-channel-templates") {
            adaptive_templates_override = true;
            has_adaptive_templates_override = true;
        } else if (arg == "--no-adaptive-channel-templates") {
            adaptive_templates_override = false;
            has_adaptive_templates_override = true;
        } else if (arg == "--weighted-correlation") {
            weighted_correlation_override = true;
            has_weighted_correlation_override = true;
        } else if (arg == "--no-weighted-correlation") {
            weighted_correlation_override = false;
            has_weighted_correlation_override = true;
        } else if (arg == "--no-adaptive-llr") {
            adaptive_llr_override = false;
            has_adaptive_llr_override = true;
        } else if (arg.compare(0, llr_scale_prefix.size(), llr_scale_prefix) == 0) {
            options.manual_llr_scale =
                parse_cli_double(arg.substr(llr_scale_prefix.size()), "--llr-scale");
            if (options.manual_llr_scale <= 0.0) {
                throw std::runtime_error("--llr-scale must be positive");
            }
            options.manual_llr_scale_set = true;
        } else if (arg == "--llr-scale" && i + 1 < argc) {
            options.manual_llr_scale = parse_cli_double(argv[++i], "--llr-scale");
            if (options.manual_llr_scale <= 0.0) {
                throw std::runtime_error("--llr-scale must be positive");
            }
            options.manual_llr_scale_set = true;
        } else if (arg == "--llr-scale") {
            throw std::runtime_error("--llr-scale requires a positive number");
        } else {
            remaining_args->push_back(arg);
        }
    }

    apply_receiver_profile_defaults(&options, selected_profile);
    if (has_timing_search_override) options.timing_search_profile = timing_search_override;
    if (has_adaptive_llr_override) options.adaptive_llr_enabled = adaptive_llr_override;
    if (has_adaptive_clock_override) {
        options.adaptive_clock_tracking_enabled = adaptive_clock_override;
    }
    if (has_adaptive_templates_override) {
        options.adaptive_channel_templates_enabled = adaptive_templates_override;
    }
    if (has_weighted_correlation_override) {
        options.weighted_correlation_enabled = weighted_correlation_override;
    }
    return options;
}

}  // namespace receiver
}  // namespace chirp
