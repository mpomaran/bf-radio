#ifndef BF_RADIO_SRC_RADIO_PROCESS_H_
#define BF_RADIO_SRC_RADIO_PROCESS_H_

#include <string>
#include <vector>

namespace radio {

std::string shell_quote(const std::string& value);
int run_command(const std::vector<std::string>& argv);

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_PROCESS_H_
