#include "src/radio/audio_io.h"

#include "src/radio/audio_levels.h"
#include "src/radio/process.h"
#include "src/radio/segment_writer.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <cstdio>
#include <csignal>
#include <filesystem>
#endif

namespace radio {
namespace {

class InputLevelController {
public:
    InputLevelController(std::string device_id,
                         double initial_level,
                         bool auto_level,
                         bool verbose)
        : device_id_(std::move(device_id)),
          current_(checked_level(initial_level)),
          upper_(std::max(0.6, std::min(1.0, current_ * 1.5))),
          auto_level_(auto_level),
          verbose_(verbose) {}

    void start() {
        set_capture_level(device_id_, current_, verbose_);
        if (auto_level_) {
            std::cerr << "Auto input level enabled: target peak 45-70%, hard limit 90%, "
                      << "fast reaction after " << kInstantHotSamples << " hot sample(s).\n";
        }
    }

    void observe(const int16_t* samples, size_t count) {
        if (!auto_level_ || count == 0) return;
        int peak_abs = 0;
        int64_t sum_squares = 0;
        size_t hot = 0;
        size_t window_hot = 0;
        int window_peak = 0;

        for (size_t i = 0; i < count; ++i) {
            const int v = std::abs(int(samples[i]));
            peak_abs = std::max(peak_abs, v);
            window_peak = std::max(window_peak, v);
            sum_squares += int64_t(samples[i]) * int64_t(samples[i]);
            if (v >= kHotThreshold) {
                ++hot;
                ++window_hot;
            }
            const bool end_of_window =
                ((i + 1) % kFastWindowSamples == 0) || (i + 1 == count);
            if (end_of_window) {
                if (window_hot >= kInstantHotSamples) {
                    reset_counters();
                    upper_ = current_;
                    if (verbose_) {
                        std::cerr << "Input fast monitor: peak "
                                  << int((double(window_peak) / 32767.0) * 100.0 + 0.5)
                                  << "%, hot " << window_hot << "/"
                                  << (((i + 1) % kFastWindowSamples == 0)
                                          ? kFastWindowSamples
                                          : ((i + 1) % kFastWindowSamples))
                                  << "\n";
                    }
                    adjust(down_step(), "fast peak above safe limit");
                    return;
                }
                window_hot = 0;
                window_peak = 0;
            }
        }
        const double peak = double(peak_abs) / 32767.0;
        const double rms = std::sqrt(double(sum_squares) / double(count)) / 32767.0;
        if (verbose_) {
            std::cerr << "Input monitor: peak " << int(peak * 100.0 + 0.5)
                      << "%, rms " << int(rms * 100.0 + 0.5)
                      << "%, hot " << hot << "/" << count
                      << ", microphone " << int(current_ * 100.0 + 0.5) << "%\n";
        }

        if (hot > 0 || peak >= kHardLimit) {
            reset_counters();
            upper_ = current_;
            adjust(down_step(), "peak above safe limit");
            return;
        }

        if (peak >= kTargetLow && peak <= kTargetHigh) {
            reset_counters();
            lower_ = current_;
            active_seen_ = true;
            return;
        }

        if (peak > kTargetHigh) {
            low_peak_buffers_ = 0;
            ++high_peak_buffers_;
            if (peak >= kImmediateLowerPeak || high_peak_buffers_ >= kHighPeakBuffersBeforeLower) {
                upper_ = current_;
                adjust(down_step(), "signal above target");
                high_peak_buffers_ = 0;
            }
            return;
        }

        if (peak >= kRaiseSignalFloor && rms >= kRaiseRmsFloor && peak < kTargetLow) {
            high_peak_buffers_ = 0;
            ++low_peak_buffers_;
            const int buffers_before_raise =
                active_seen_ ? kLowPeakBuffersBeforeRaise : kColdStartLowPeakBuffersBeforeRaise;
            if (low_peak_buffers_ >= buffers_before_raise) {
                lower_ = current_;
                adjust(up_step(), "sustained signal below target");
                low_peak_buffers_ = 0;
            }
            return;
        }

        reset_counters();
    }

private:
    static double checked_level(double level) {
        if (level < 0.0 || level > 1.0) {
            throw std::runtime_error("input level must be between 0.0 and 1.0");
        }
        return level;
    }

    void adjust(double next, const char* reason) {
        next = std::max(0.01, std::min(1.0, next));
        if (std::abs(next - current_) < 0.005) return;
        current_ = next;
        std::cerr << "Auto input level: " << reason << "; setting microphone to "
                  << int(current_ * 100.0 + 0.5) << "%\n";
        set_capture_level(device_id_, current_, verbose_);
    }

    double down_step() const {
        return (lower_ + upper_) / 2.0;
    }

    double up_step() const {
        return (lower_ + upper_) / 2.0;
    }

    void reset_counters() {
        low_peak_buffers_ = 0;
        high_peak_buffers_ = 0;
    }

    static constexpr int kHotThreshold = 31800;
    static constexpr size_t kFastWindowSamples = 32;
    static constexpr size_t kInstantHotSamples = 1;
    static constexpr double kRaiseSignalFloor = 0.18;
    static constexpr double kRaiseRmsFloor = 0.03;
    static constexpr double kTargetLow = 0.45;
    static constexpr double kTargetHigh = 0.70;
    static constexpr double kImmediateLowerPeak = 0.85;
    static constexpr double kHardLimit = 0.90;
    static constexpr int kLowPeakBuffersBeforeRaise = 25;
    static constexpr int kColdStartLowPeakBuffersBeforeRaise = 50;
    static constexpr int kHighPeakBuffersBeforeLower = 2;

    std::string device_id_;
    double current_ = 0.4;
    double lower_ = 0.0;
    double upper_ = 0.6;
    int low_peak_buffers_ = 0;
    int high_peak_buffers_ = 0;
    bool active_seen_ = false;
    bool auto_level_ = true;
    bool verbose_ = false;
};

}  // namespace

#ifdef _WIN32
namespace {

std::atomic<bool> g_stop_recording{false};

BOOL WINAPI recording_ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_SHUTDOWN_EVENT) {
        g_stop_recording.store(true);
        return TRUE;
    }
    return FALSE;
}

void append_completed_buffers(std::vector<std::vector<int16_t>>* buffers,
                              std::vector<WAVEHDR>* headers,
                              InputLevelController* input_level,
                              SegmentWriter* writer,
                              bool requeue,
                              HWAVEIN in) {
    for (size_t i = 0; i < headers->size(); ++i) {
        WAVEHDR& hdr = (*headers)[i];
        if (hdr.dwFlags & WHDR_DONE) {
            const size_t count = hdr.dwBytesRecorded / sizeof(int16_t);
            if (count > 0) {
                input_level->observe((*buffers)[i].data(), count);
                writer->append((*buffers)[i].data(), count);
            }
            hdr.dwFlags &= ~WHDR_DONE;
            hdr.dwBytesRecorded = 0;
            if (requeue) waveInAddBuffer(in, &hdr, sizeof(WAVEHDR));
        }
    }
}

}  // namespace

void play_audio(const AudioBuffer& audio, const std::string& device_id) {
    if (audio.channels == 0 || audio.channels > 2) {
        throw std::runtime_error("Windows playback supports 1 or 2 channels");
    }
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = audio.channels;
    fmt.nSamplesPerSec = audio.sample_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = uint16_t(audio.channels * 2);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    const UINT dev = device_id.empty() ? WAVE_MAPPER : UINT(std::stoul(device_id));
    HWAVEOUT out = nullptr;
    HANDLE done = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!done) throw std::runtime_error("CreateEvent failed");
    MMRESULT rc = waveOutOpen(&out, dev, &fmt, DWORD_PTR(done), 0, CALLBACK_EVENT);
    if (rc != MMSYSERR_NOERROR) {
        CloseHandle(done);
        throw std::runtime_error("waveOutOpen failed");
    }
    WAVEHDR hdr = {};
    hdr.lpData = reinterpret_cast<LPSTR>(const_cast<int16_t*>(audio.samples.data()));
    hdr.dwBufferLength = DWORD(audio.samples.size() * sizeof(int16_t));
    if (waveOutPrepareHeader(out, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR ||
        waveOutWrite(out, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        waveOutClose(out);
        CloseHandle(done);
        throw std::runtime_error("waveOutWrite failed");
    }
    while (!(hdr.dwFlags & WHDR_DONE)) WaitForSingleObject(done, 100);
    waveOutUnprepareHeader(out, &hdr, sizeof(hdr));
    waveOutClose(out);
    CloseHandle(done);
}

void record_audio_to_segments(const std::string& device_id,
                              const std::string& output_prefix,
                              uint32_t sample_rate,
                              uint16_t channels,
                              uint32_t segment_seconds,
                              double input_level,
                              bool auto_input_level,
                              uint32_t duration_seconds,
                              bool verbose) {
    if (channels == 0 || channels > 2) throw std::runtime_error("Windows capture supports 1 or 2 channels");
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = channels;
    fmt.nSamplesPerSec = sample_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = uint16_t(channels * 2);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    const UINT dev = device_id.empty() ? WAVE_MAPPER : UINT(std::stoul(device_id));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HWAVEIN in = nullptr;
    if (waveInOpen(&in, dev, &fmt, DWORD_PTR(event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(event);
        throw std::runtime_error("waveInOpen failed");
    }
    const size_t samples_per_buffer = std::max<size_t>(64, size_t(sample_rate) * channels / 50);
    std::vector<std::vector<int16_t>> buffers(4, std::vector<int16_t>(samples_per_buffer));
    std::vector<WAVEHDR> headers(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        headers[i].dwBufferLength = DWORD(buffers[i].size() * sizeof(int16_t));
        waveInPrepareHeader(in, &headers[i], sizeof(WAVEHDR));
        waveInAddBuffer(in, &headers[i], sizeof(WAVEHDR));
    }
    InputLevelController level_controller(device_id, input_level, auto_input_level, verbose);
    level_controller.start();
    SegmentWriter writer(output_prefix, sample_rate, channels, segment_seconds, verbose);
    std::cerr << "Recording; press Ctrl+C to stop.\n";
    g_stop_recording.store(false);
    SetConsoleCtrlHandler(recording_ctrl_handler, TRUE);
    waveInStart(in);
    const auto started = std::chrono::steady_clock::now();
    while (!g_stop_recording.load()) {
        WaitForSingleObject(event, 200);
        append_completed_buffers(&buffers, &headers, &level_controller, &writer, true, in);
        if (duration_seconds > 0 &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(duration_seconds)) {
            break;
        }
    }
    waveInStop(in);
    waveInReset(in);
    append_completed_buffers(&buffers, &headers, &level_controller, &writer, false, in);
    for (size_t i = 0; i < headers.size(); ++i) {
        waveInUnprepareHeader(in, &headers[i], sizeof(WAVEHDR));
    }
    waveInClose(in);
    CloseHandle(event);
    writer.close();
    SetConsoleCtrlHandler(recording_ctrl_handler, FALSE);
    std::cerr << "Stopped. Wrote " << writer.segments_written() << " file(s), "
              << writer.total_samples_written() << " sample(s).\n";
}
#else
void play_audio(const AudioBuffer& audio, const std::string& device_id) {
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "bf_radio_tx_tmp.wav";
    write_wav_pcm16(tmp.string(), audio);
    std::vector<std::string> cmd = {"aplay", "-q"};
    if (!device_id.empty()) {
        cmd.push_back("-D");
        cmd.push_back(device_id);
    }
    cmd.push_back(tmp.string());
    const int rc = run_command(cmd);
    std::filesystem::remove(tmp);
    if (rc != 0) throw std::runtime_error("aplay failed");
}

void record_audio_to_segments(const std::string& device_id,
                              const std::string& output_prefix,
                              uint32_t sample_rate,
                              uint16_t channels,
                              uint32_t segment_seconds,
                              double input_level,
                              bool auto_input_level,
                              uint32_t duration_seconds,
                              bool verbose) {
    std::vector<std::string> cmd = {
        "arecord", "-q", "-f", "S16_LE", "-r", std::to_string(sample_rate),
        "-c", std::to_string(channels), "-t", "raw"};
    if (duration_seconds > 0) {
        cmd.push_back("-d");
        cmd.push_back(std::to_string(duration_seconds));
    }
    if (!device_id.empty()) {
        cmd.insert(cmd.begin() + 1, device_id);
        cmd.insert(cmd.begin() + 1, "-D");
    }
    std::string shell;
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (i) shell += " ";
        shell += shell_quote(cmd[i]);
    }
    FILE* pipe = popen(shell.c_str(), "r");
    if (!pipe) throw std::runtime_error("cannot start arecord");
    InputLevelController level_controller(device_id, input_level, auto_input_level, verbose);
    level_controller.start();
    SegmentWriter writer(output_prefix, sample_rate, channels, segment_seconds, verbose);
    std::vector<int16_t> buffer(std::max<size_t>(64, size_t(sample_rate) * channels / 50));
    while (true) {
        const size_t got = std::fread(buffer.data(), sizeof(int16_t), buffer.size(), pipe);
        if (got > 0) {
            level_controller.observe(buffer.data(), got);
            writer.append(buffer.data(), got);
        }
        if (got < buffer.size()) {
            if (std::feof(pipe)) break;
            if (std::ferror(pipe)) {
                pclose(pipe);
                throw std::runtime_error("error while reading arecord output");
            }
        }
    }
    writer.close();
    const int rc = pclose(pipe);
    if (rc != 0) throw std::runtime_error("arecord failed");
}
#endif

}  // namespace radio
