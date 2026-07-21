#include "src/radio/audio_levels.h"

#include "src/radio/process.h"

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <endpointvolume.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#else
#include <vector>
#endif

namespace radio {
namespace {

double checked_level(double level) {
    if (level < 0.0 || level > 1.0) {
        throw std::runtime_error("input level must be between 0.0 and 1.0");
    }
    return level;
}

#ifdef _WIN32
constexpr PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}},
    14};

struct ComInit {
    ComInit() {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            throw std::runtime_error("CoInitializeEx failed");
        }
    }
    ~ComInit() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
    HRESULT hr = S_OK;
};

template <typename T>
void release_if(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

std::string wide_to_utf8(const wchar_t* w) {
    if (!w) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out(size_t(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), need, nullptr, nullptr);
    return out;
}

std::string endpoint_name(IMMDevice* device) {
    IPropertyStore* props = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) return {};
    PROPVARIANT var;
    PropVariantInit(&var);
    std::string name;
    if (SUCCEEDED(props->GetValue(kPkeyDeviceFriendlyName, &var)) && var.vt == VT_LPWSTR) {
        name = wide_to_utf8(var.pwszVal);
    }
    PropVariantClear(&var);
    props->Release();
    return name;
}

std::string wave_in_name(UINT index) {
    WAVEINCAPSW caps = {};
    if (waveInGetDevCapsW(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return {};
    return wide_to_utf8(caps.szPname);
}

std::string wave_out_name(UINT index) {
    WAVEOUTCAPSW caps = {};
    if (waveOutGetDevCapsW(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return {};
    return wide_to_utf8(caps.szPname);
}

bool same_audio_name(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return a == b || a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

IMMDevice* endpoint_by_index(IMMDeviceEnumerator* enumerator,
                             EDataFlow flow,
                             const std::string& device_id,
                             const std::string& target_name) {
    if (device_id.empty()) {
        IMMDevice* selected = nullptr;
        const HRESULT hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &selected);
        if (FAILED(hr)) throw std::runtime_error("GetDefaultAudioEndpoint failed");
        return selected;
    }

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) throw std::runtime_error("EnumAudioEndpoints failed");
    UINT count = 0;
    collection->GetCount(&count);
    const UINT requested = UINT(std::stoul(device_id));
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* candidate = nullptr;
        if (SUCCEEDED(collection->Item(i, &candidate))) {
            const std::string name = endpoint_name(candidate);
            if (same_audio_name(name, target_name)) {
                collection->Release();
                return candidate;
            }
            candidate->Release();
        }
    }
    if (requested >= count) {
        collection->Release();
        throw std::runtime_error("capture endpoint index is out of range: " + device_id);
    }
    IMMDevice* selected = nullptr;
    hr = collection->Item(requested, &selected);
    collection->Release();
    if (FAILED(hr)) throw std::runtime_error("cannot get capture endpoint: " + device_id);
    return selected;
}
#endif

}  // namespace

void set_endpoint_level(const std::string& device_id,
                        double level,
                        bool verbose,
                        bool capture) {
    level = checked_level(level);
#ifdef _WIN32
    ComInit com;
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) throw std::runtime_error("CoCreateInstance(MMDeviceEnumerator) failed");

    IMMDevice* device = nullptr;
    IAudioEndpointVolume* volume = nullptr;
    try {
        const UINT requested = device_id.empty() ? 0 : UINT(std::stoul(device_id));
        const std::string target_name = capture ? wave_in_name(requested) : wave_out_name(requested);
        device = endpoint_by_index(enumerator, capture ? eCapture : eRender, device_id,
                                   target_name);
        const std::string name = endpoint_name(device);
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&volume));
        if (FAILED(hr)) throw std::runtime_error("capture endpoint volume is not available");
        hr = volume->SetMasterVolumeLevelScalar(float(level), nullptr);
        if (FAILED(hr)) throw std::runtime_error("SetMasterVolumeLevelScalar failed");
        if (verbose) {
            std::cerr << "Set " << (capture ? "capture input" : "playback output")
                      << " level to " << int(level * 100.0 + 0.5)
                      << "% for " << (name.empty() ? device_id : name) << "\n";
        }
    } catch (...) {
        release_if(&volume);
        release_if(&device);
        release_if(&enumerator);
        throw;
    }
    release_if(&volume);
    release_if(&device);
    release_if(&enumerator);
#else
    const int percent = int(level * 100.0 + 0.5);
    std::vector<std::string> cmd = {"amixer"};
    if (!device_id.empty()) {
        cmd.push_back("-D");
        cmd.push_back(device_id);
    }
    cmd.push_back("sset");
    cmd.push_back(capture ? "Capture" : "Playback");
    cmd.push_back(std::to_string(percent) + "%");
    int rc = run_command(cmd);
    if (rc != 0) {
        cmd.clear();
        cmd = {"amixer"};
        if (!device_id.empty()) {
            cmd.push_back("-D");
            cmd.push_back(device_id);
        }
        cmd.push_back("sset");
        cmd.push_back(capture ? "Mic" : "Master");
        cmd.push_back(std::to_string(percent) + "%");
        rc = run_command(cmd);
    }
    if (rc != 0) throw std::runtime_error("amixer failed while setting audio level");
    if (verbose) {
        std::cerr << "Set " << (capture ? "capture input" : "playback output")
                  << " level to " << percent << "%\n";
    }
#endif
}

void set_playback_level(const std::string& device_id, double level, bool verbose) {
    set_endpoint_level(device_id, level, verbose, false);
}

void set_capture_level(const std::string& device_id, double level, bool verbose) {
    set_endpoint_level(device_id, level, verbose, true);
}

}  // namespace radio
