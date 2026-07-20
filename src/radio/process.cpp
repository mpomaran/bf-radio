#include "src/radio/process.h"

#include <cstdlib>
#include <sstream>

namespace radio {

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

int run_command(const std::vector<std::string>& argv) {
    std::ostringstream cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd << ' ';
        cmd << shell_quote(argv[i]);
    }
    return std::system(cmd.str().c_str());
}

}  // namespace radio
