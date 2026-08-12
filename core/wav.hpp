// wav.hpp — minimal PCM WAV I/O (dependency-free).
// Reads 8/16/24/32-bit PCM and 32-bit float WAV, mono-izes, returns float
// samples in [-1, 1]. Writes 16-bit PCM mono.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nova {

struct Wav {
    int sample_rate = 0;
    std::vector<float> samples;  // mono
};

// Throws std::runtime_error on malformed/unsupported files.
Wav read_wav(const std::string& path);

// Clamps to [-1, 1].
void write_wav(const std::string& path, int sample_rate,
               const std::vector<float>& samples);

}  // namespace nova
