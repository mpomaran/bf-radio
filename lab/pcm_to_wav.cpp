#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/*
  pcm_to_wav.cpp — Convert raw PCM audio to WAV format.
  
  Designed to work with p4modem output:
    - 16-bit signed PCM
    - 8000 Hz sample rate
    - mono (1 channel)
  
  Usage:
    ./pcm_to_wav input.pcm output.wav [sample_rate] [channels]
  
  Defaults (matching p4modem):
    - sample_rate: 8000 Hz
    - channels: 1 (mono)
*/

struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
};

struct FormatSubchunk {
    char subchunk1_id[4] = {'f', 'm', 't', ' '};
    uint32_t subchunk1_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate = 8000;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 16;
};

struct DataSubchunk {
    char subchunk2_id[4] = {'d', 'a', 't', 'a'};
    uint32_t subchunk2_size = 0;
};

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            std::cerr <<
                "Usage:\n"
                "  " << argv[0] << " input.pcm output.wav [sample_rate] [channels]\n"
                "\n"
                "Defaults:\n"
                "  sample_rate: 8000 Hz\n"
                "  channels: 1 (mono)\n"
                "  bits_per_sample: 16\n";
            return 1;
        }

        std::string in_path = argv[1];
        std::string out_path = argv[2];
        uint32_t sample_rate = 8000;
        uint16_t channels = 1;

        if (argc > 3) sample_rate = std::stoul(argv[3]);
        if (argc > 4) channels = std::stoul(argv[4]);

        if (channels < 1 || channels > 16) {
            throw std::runtime_error("Channels must be 1-16");
        }

        // Read PCM data
        std::ifstream pcm_file(in_path, std::ios::binary);
        if (!pcm_file) {
            throw std::runtime_error("Cannot open input PCM file");
        }

        std::vector<uint8_t> pcm_data(
            (std::istreambuf_iterator<char>(pcm_file)),
            std::istreambuf_iterator<char>()
        );
        pcm_file.close();

        if (pcm_data.size() % 2) {
            std::cerr << "Warning: PCM data size is odd, last byte will be dropped\n";
            pcm_data.pop_back();
        }

        uint32_t num_samples = pcm_data.size() / 2;
        uint16_t bits_per_sample = 16;

        // Prepare headers
        WAVHeader riff_header;
        FormatSubchunk fmt_chunk;
        DataSubchunk data_chunk;

        fmt_chunk.num_channels = channels;
        fmt_chunk.sample_rate = sample_rate;
        fmt_chunk.byte_rate = sample_rate * channels * bits_per_sample / 8;
        fmt_chunk.block_align = channels * bits_per_sample / 8;
        fmt_chunk.bits_per_sample = bits_per_sample;

        data_chunk.subchunk2_size = pcm_data.size();

        uint32_t file_size = 4 + (8 + fmt_chunk.subchunk1_size) + (8 + data_chunk.subchunk2_size);
        riff_header.chunk_size = file_size;

        // Write WAV file
        std::ofstream wav_file(out_path, std::ios::binary);
        if (!wav_file) {
            throw std::runtime_error("Cannot open output WAV file");
        }

        wav_file.write(reinterpret_cast<const char*>(&riff_header), sizeof(riff_header));
        wav_file.write(reinterpret_cast<const char*>(&fmt_chunk), sizeof(fmt_chunk));
        wav_file.write(reinterpret_cast<const char*>(&data_chunk), sizeof(data_chunk));
        wav_file.write(reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size());

        wav_file.close();

        std::cerr << "Converted " << pcm_data.size() << " bytes PCM -> "
                  << num_samples << " samples\n"
                  << "Duration: " << double(num_samples) / sample_rate << " s\n"
                  << "Sample rate: " << sample_rate << " Hz\n"
                  << "Channels: " << channels << "\n"
                  << "Output: " << out_path << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
