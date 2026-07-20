#include "src/radio/devices.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <fstream>
#endif

namespace radio {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool likely_digirig_text(const std::string& s) {
    const std::string v = lower(s);
    return v.find("digirig") != std::string::npos ||
           v.find("vid_0d8c&pid_0012") != std::string::npos ||
           v.find("0d8c:0012") != std::string::npos ||
           v.find("usb audio device") != std::string::npos ||
           v.find("cm108") != std::string::npos ||
           v.find("cm119") != std::string::npos ||
           v.find("c-media") != std::string::npos ||
           v.find("vid_10c4&pid_ea60") != std::string::npos ||
           v.find("10c4:ea60") != std::string::npos ||
           v.find("cp210") != std::string::npos ||
           v.find("silicon labs") != std::string::npos ||
           v.find("silabser") != std::string::npos ||
           v.find("ch340") != std::string::npos ||
           v.find("ftdi") != std::string::npos;
}

void add_unique(std::vector<DeviceCandidate>* out,
                std::set<std::string>* seen,
                const std::string& id,
                const std::string& name) {
    if (id.empty() || seen->count(id)) return;
    seen->insert(id);
    out->push_back({id, name.empty() ? id : name, likely_digirig_text(id + " " + name)});
}

#ifdef _WIN32
std::string wide_to_utf8(const wchar_t* w) {
    if (!w) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out(size_t(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), need, nullptr, nullptr);
    return out;
}

std::string reg_sz(HKEY key, const char* value_name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExA(key, value_name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        type != REG_SZ || bytes == 0) {
        return {};
    }
    std::string out(bytes, '\0');
    if (RegQueryValueExA(key, value_name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(out.data()), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

void collect_serial_port_names(HKEY key,
                               const std::string& path,
                               const std::string& best_name,
                               std::map<std::string, std::string>* names) {
    HKEY sub = nullptr;
    if (RegOpenKeyExA(key, path.c_str(), 0, KEY_READ, &sub) != ERROR_SUCCESS) return;

    std::string name = reg_sz(sub, "FriendlyName");
    if (name.empty()) name = reg_sz(sub, "DeviceDesc");
    if (name.empty()) name = reg_sz(sub, "DriverDesc");
    if (name.empty()) name = best_name;

    HKEY params = nullptr;
    if (RegOpenKeyExA(sub, "Device Parameters", 0, KEY_READ, &params) == ERROR_SUCCESS) {
        const std::string port = reg_sz(params, "PortName");
        if (!port.empty()) {
            std::string decorated = name.empty() ? port : name;
            decorated += " [" + path + "]";
            (*names)[port] = decorated;
        }
        RegCloseKey(params);
    }

    char child[256];
    for (DWORD index = 0;; ++index) {
        DWORD child_len = sizeof(child);
        const LONG rc = RegEnumKeyExA(sub, index, child, &child_len, nullptr, nullptr, nullptr,
                                      nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS) {
            collect_serial_port_names(key, path + "\\" + std::string(child, child_len), name,
                                      names);
        }
    }
    RegCloseKey(sub);
}

std::map<std::string, std::string> serial_friendly_names() {
    std::map<std::string, std::string> names;
    collect_serial_port_names(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum", "", &names);
    return names;
}
#endif

}  // namespace

std::vector<DeviceCandidate> list_serial_devices() {
    std::vector<DeviceCandidate> out;
    std::set<std::string> seen;
#ifdef _WIN32
    const auto friendly = serial_friendly_names();
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
        char value_name[256];
        char data[256];
        for (DWORD index = 0;; ++index) {
            DWORD value_len = sizeof(value_name);
            DWORD data_len = sizeof(data);
            DWORD type = 0;
            LONG rc = RegEnumValueA(key, index, value_name, &value_len, nullptr, &type,
                                    reinterpret_cast<LPBYTE>(data), &data_len);
            if (rc == ERROR_NO_MORE_ITEMS) break;
            if (rc == ERROR_SUCCESS && type == REG_SZ) {
                const std::string port(data);
                auto it = friendly.find(port);
                const std::string name = it == friendly.end()
                                             ? std::string(value_name, value_len)
                                             : it->second;
                add_unique(&out, &seen, port, name);
            }
        }
        RegCloseKey(key);
    }
#else
    const std::vector<std::string> dirs = {"/dev/serial/by-id", "/dev"};
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!std::filesystem::exists(d, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
            const std::string name = e.path().filename().string();
            if (d == "/dev" && !(name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0)) {
                continue;
            }
            add_unique(&out, &seen, e.path().string(), name);
        }
    }
#endif
    return out;
}

std::vector<DeviceCandidate> list_playback_devices() {
    std::vector<DeviceCandidate> out;
    std::set<std::string> seen;
#ifdef _WIN32
    const UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEOUTCAPSW caps = {};
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            add_unique(&out, &seen, std::to_string(i), wide_to_utf8(caps.szPname));
        }
    }
#else
    std::ifstream cards("/proc/asound/cards");
    std::string line;
    while (std::getline(cards, line)) {
        const std::string trimmed = line;
        if (trimmed.find('[') != std::string::npos) {
            add_unique(&out, &seen, "plughw:" + std::to_string(out.size()), trimmed);
        }
    }
#endif
    return out;
}

std::vector<DeviceCandidate> list_recording_devices() {
#ifdef _WIN32
    std::vector<DeviceCandidate> out;
    std::set<std::string> seen;
    const UINT n = waveInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEINCAPSW caps = {};
        if (waveInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            add_unique(&out, &seen, std::to_string(i), wide_to_utf8(caps.szPname));
        }
    }
    return out;
#else
    return list_playback_devices();
#endif
}

int choose_candidate(const std::vector<DeviceCandidate>& devices,
                     const std::string& requested,
                     bool interactive) {
    if (!requested.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].id == requested || devices[i].name.find(requested) != std::string::npos) {
                return int(i);
            }
        }
        throw std::runtime_error("requested device not found: " + requested);
    }
    std::vector<int> likely;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].likely_digirig) likely.push_back(int(i));
    }
    if (likely.size() == 1) return likely[0];
    if (!interactive) return devices.empty() ? -1 : 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "[" << i << "] " << devices[i].id << "  " << devices[i].name
                  << (devices[i].likely_digirig ? "  (looks like Digirig)" : "") << "\n";
    }
    std::cout << "Choose device index: ";
    int selected = -1;
    std::cin >> selected;
    if (selected < 0 || selected >= int(devices.size())) throw std::runtime_error("bad selection");
    return selected;
}

}  // namespace radio
