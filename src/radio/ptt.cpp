#include "src/radio/ptt.h"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace radio {

RtsPtt::RtsPtt(const std::string& port) {
#ifdef _WIN32
    std::string path = port;
    if (path.rfind("\\\\.\\", 0) != 0) path = "\\\\.\\" + path;
    handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        throw std::runtime_error("cannot open serial PTT port: " + port);
    }
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(handle_, &dcb)) {
        dcb.BaudRate = CBR_9600;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(handle_, &dcb);
    }
#else
    fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw std::runtime_error("cannot open serial PTT port: " + port);
#endif
}

RtsPtt::~RtsPtt() {
    try {
        set(false);
    } catch (...) {
    }
#ifdef _WIN32
    if (handle_) CloseHandle(handle_);
#else
    if (fd_ >= 0) close(fd_);
#endif
}

void RtsPtt::set(bool enabled) {
#ifdef _WIN32
    if (!handle_ || !EscapeCommFunction(handle_, enabled ? SETRTS : CLRRTS)) {
        throw std::runtime_error("cannot change RTS state");
    }
#else
    int bit = TIOCM_RTS;
    if (ioctl(fd_, enabled ? TIOCMBIS : TIOCMBIC, &bit) < 0) {
        throw std::runtime_error("cannot change RTS state");
    }
#endif
}

}  // namespace radio
