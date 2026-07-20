#ifndef BF_RADIO_SRC_RADIO_PTT_H_
#define BF_RADIO_SRC_RADIO_PTT_H_

#include <string>

namespace radio {

class RtsPtt {
public:
    explicit RtsPtt(const std::string& port);
    ~RtsPtt();
    RtsPtt(const RtsPtt&) = delete;
    RtsPtt& operator=(const RtsPtt&) = delete;
    void set(bool enabled);

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_PTT_H_
