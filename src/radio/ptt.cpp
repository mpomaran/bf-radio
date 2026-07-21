#include "src/radio/ptt.h"

#include <chrono>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace radio {
namespace {

#ifdef _WIN32
std::string windows_error_message(const char* what) {
    const DWORD err = GetLastError();
    return std::string(what) + " (GetLastError=" + std::to_string(err) + ")";
}

void close_handle(void** handle) {
    if (*handle) {
        CloseHandle(*handle);
        *handle = nullptr;
    }
}
#endif

}  // namespace

RtsPtt::RtsPtt(const std::string& port, bool active_low) : active_low_(active_low) {
#ifdef _WIN32
    std::string path = port;
    if (path.rfind("\\\\.\\", 0) != 0) path = "\\\\.\\" + path;
    handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        throw std::runtime_error(
            windows_error_message(("cannot open serial PTT port: " + port).c_str()));
    }
    SetupComm(handle_, 4096, 4096);
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) {
        close_handle(&handle_);
        throw std::runtime_error(windows_error_message("GetCommState failed for serial PTT port"));
    }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(handle_, &dcb)) {
        close_handle(&handle_);
        throw std::runtime_error(windows_error_message("SetCommState failed for serial PTT port"));
    }
    if (!EscapeCommFunction(handle_, CLRDTR)) {
        close_handle(&handle_);
        throw std::runtime_error(windows_error_message("cannot initialize serial PTT lines"));
    }
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    set(false);
#else
    fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw std::runtime_error("cannot open serial PTT port: " + port);
    set(false);
#endif
}

RtsPtt::~RtsPtt() {
    force_off();
#ifdef _WIN32
    if (handle_) CloseHandle(handle_);
#else
    if (fd_ >= 0) close(fd_);
#endif
}

void RtsPtt::set(bool enabled) {
#ifdef _WIN32
    const bool rts_high = active_low_ ? !enabled : enabled;
    if (!handle_) throw std::runtime_error("cannot change RTS state: port is closed");

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) {
        throw std::runtime_error(windows_error_message("GetCommState failed while changing RTS"));
    }
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = rts_high ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    if (!SetCommState(handle_, &dcb)) {
        throw std::runtime_error(windows_error_message("SetCommState failed while changing RTS"));
    }
    if (!rts_high) EscapeCommFunction(handle_, CLRRTS);
#else
    int bit = TIOCM_RTS;
    const bool rts_high = active_low_ ? !enabled : enabled;
    if (ioctl(fd_, rts_high ? TIOCMBIS : TIOCMBIC, &bit) < 0) {
        throw std::runtime_error("cannot change RTS state");
    }
#endif
}

void RtsPtt::force_off() noexcept {
    (void)force_off_until(std::chrono::milliseconds(250), std::chrono::milliseconds(25));
}

bool RtsPtt::force_off_until(std::chrono::milliseconds timeout,
                             std::chrono::milliseconds retry_interval) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool accepted = false;
    do {
        if (try_force_off_once()) accepted = true;
        if (accepted) break;
        std::this_thread::sleep_for(retry_interval);
    } while (std::chrono::steady_clock::now() < deadline);
    return accepted;
}

bool RtsPtt::try_force_off_once() noexcept {
    bool accepted = false;
    try {
        set(false);
        accepted = true;
    } catch (...) {
    }
#ifdef _WIN32
    if (handle_) {
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(handle_, &dcb)) {
            dcb.fOutxCtsFlow = FALSE;
            dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_DISABLE;
            dcb.fRtsControl =
                active_low_ ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
            if (SetCommState(handle_, &dcb)) accepted = true;
        }
        if (!active_low_ && EscapeCommFunction(handle_, CLRRTS)) accepted = true;
        if (EscapeCommFunction(handle_, CLRDTR)) accepted = true;
        FlushFileBuffers(handle_);
    }
#endif
    return accepted;
}

}  // namespace radio
