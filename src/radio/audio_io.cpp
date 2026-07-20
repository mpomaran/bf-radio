#include "src/radio/audio_io.h"

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

void apply_gain(std::vector<int16_t>* samples, size_t count, double gain) {
    if (gain == 1.0) return;
    if (gain < 0.0) throw std::runtime_error("gain must be >= 0");
    for (size_t i = 0; i < count; ++i) {
        int v = int(std::lround(double((*samples)[i]) * gain));
        v = std::max(-32768, std::min(32767, v));
        (*samples)[i] = int16_t(v);
    }
}

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
                              double gain,
                              SegmentWriter* writer,
                              bool requeue,
                              HWAVEIN in) {
    for (size_t i = 0; i < headers->size(); ++i) {
        WAVEHDR& hdr = (*headers)[i];
        if (hdr.dwFlags & WHDR_DONE) {
            const size_t count = hdr.dwBytesRecorded / sizeof(int16_t);
            if (count > 0) {
                apply_gain(&(*buffers)[i], count, gain);
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
                              double gain,
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
    const size_t samples_per_buffer = size_t(sample_rate) * channels / 5;
    std::vector<std::vector<int16_t>> buffers(4, std::vector<int16_t>(samples_per_buffer));
    std::vector<WAVEHDR> headers(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        headers[i].dwBufferLength = DWORD(buffers[i].size() * sizeof(int16_t));
        waveInPrepareHeader(in, &headers[i], sizeof(WAVEHDR));
        waveInAddBuffer(in, &headers[i], sizeof(WAVEHDR));
    }
    SegmentWriter writer(output_prefix, sample_rate, channels, segment_seconds, verbose);
    std::cerr << "Recording; press Ctrl+C to stop.\n";
    g_stop_recording.store(false);
    SetConsoleCtrlHandler(recording_ctrl_handler, TRUE);
    waveInStart(in);
    const auto started = std::chrono::steady_clock::now();
    while (!g_stop_recording.load()) {
        WaitForSingleObject(event, 200);
        append_completed_buffers(&buffers, &headers, gain, &writer, true, in);
        if (duration_seconds > 0 &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(duration_seconds)) {
            break;
        }
    }
    waveInStop(in);
    waveInReset(in);
    append_completed_buffers(&buffers, &headers, gain, &writer, false, in);
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
                              double gain,
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
    SegmentWriter writer(output_prefix, sample_rate, channels, segment_seconds, verbose);
    std::vector<int16_t> buffer(size_t(sample_rate) * channels / 5);
    while (true) {
        const size_t got = std::fread(buffer.data(), sizeof(int16_t), buffer.size(), pipe);
        if (got > 0) {
            apply_gain(&buffer, got, gain);
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
