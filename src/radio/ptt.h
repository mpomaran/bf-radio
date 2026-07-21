#ifndef BF_RADIO_SRC_RADIO_PTT_H_
#define BF_RADIO_SRC_RADIO_PTT_H_

#include <chrono>
#include <string>

namespace radio {

class RtsPtt {
public:
    explicit RtsPtt(const std::string& port, bool active_low = false);
    ~RtsPtt();
    RtsPtt(const RtsPtt&) = delete;
    RtsPtt& operator=(const RtsPtt&) = delete;
    void set(bool enabled);
    void force_off() noexcept;
    bool force_off_until(std::chrono::milliseconds timeout,
                         std::chrono::milliseconds retry_interval) noexcept;

private:
    bool try_force_off_once() noexcept;

#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    bool active_low_ = false;
};

}  // namespace radio

#endif  // BF_RADIO_SRC_RADIO_PTT_H_
