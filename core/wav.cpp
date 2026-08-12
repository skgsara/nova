// wav.cpp
#include "wav.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace nova {
namespace {

uint32_t rd_u32(std::istream& s) {
    unsigned char b[4];
    s.read(reinterpret_cast<char*>(b), 4);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
           (uint32_t(b[3]) << 24);
}
uint16_t rd_u16(std::istream& s) {
    unsigned char b[2];
    s.read(reinterpret_cast<char*>(b), 2);
    return uint16_t(b[0]) | (uint16_t(b[1]) << 8);
}
void wr_u32(std::ostream& s, uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v),
                          static_cast<unsigned char>(v >> 8),
                          static_cast<unsigned char>(v >> 16),
                          static_cast<unsigned char>(v >> 24)};
    s.write(reinterpret_cast<const char*>(b), 4);
}
void wr_u16(std::ostream& s, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v),
                          static_cast<unsigned char>(v >> 8)};
    s.write(reinterpret_cast<const char*>(b), 2);
}

}  // namespace

Wav read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    char tag[4];
    f.read(tag, 4);
    if (std::memcmp(tag, "RIFF", 4) != 0)
        throw std::runtime_error("not a RIFF file: " + path);
    rd_u32(f);  // riff size
    f.read(tag, 4);
    if (std::memcmp(tag, "WAVE", 4) != 0)
        throw std::runtime_error("not a WAVE file: " + path);

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<unsigned char> data;
    bool have_fmt = false, have_data = false;

    while (f && !(have_fmt && have_data)) {
        f.read(tag, 4);
        if (!f) break;
        uint32_t size = rd_u32(f);
        if (std::memcmp(tag, "fmt ", 4) == 0) {
            fmt = rd_u16(f);
            channels = rd_u16(f);
            rate = rd_u32(f);
            rd_u32(f);       // byte rate
            rd_u16(f);       // block align
            bits = rd_u16(f);
            if (size > 16) f.seekg(size - 16, std::ios::cur);
            have_fmt = true;
        } else if (std::memcmp(tag, "data", 4) == 0) {
            data.resize(size);
            f.read(reinterpret_cast<char*>(data.data()), size);
            have_data = true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);  // chunks are padded
        }
    }
    if (!have_fmt || !have_data)
        throw std::runtime_error("malformed WAV (missing fmt/data): " + path);
    if (channels < 1) throw std::runtime_error("WAV with 0 channels: " + path);

    Wav w;
    w.sample_rate = static_cast<int>(rate);
    size_t frames = 0;
    const unsigned char* p = data.data();

    auto emit = [&](float v) { w.samples.push_back(v); };

    if (fmt == 1 && bits == 16) {
        frames = data.size() / (2 * channels);
        for (size_t i = 0; i < frames; i++) {
            int32_t acc = 0;
            for (int c = 0; c < channels; c++, p += 2) {
                int16_t v = static_cast<int16_t>(p[0] | (p[1] << 8));
                acc += v;
            }
            emit(static_cast<float>(acc) / (32768.0f * channels));
        }
    } else if (fmt == 1 && bits == 8) {
        frames = data.size() / channels;
        for (size_t i = 0; i < frames; i++) {
            int32_t acc = 0;
            for (int c = 0; c < channels; c++) acc += int(*p++) - 128;
            emit(static_cast<float>(acc) / (128.0f * channels));
        }
    } else if (fmt == 1 && bits == 24) {
        frames = data.size() / (3 * channels);
        for (size_t i = 0; i < frames; i++) {
            int32_t acc = 0;
            for (int c = 0; c < channels; c++, p += 3) {
                int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) |
                            (int32_t(p[2]) << 16);
                if (v & 0x800000) v -= 0x1000000;
                acc += v;
            }
            emit(static_cast<float>(acc) / (8388608.0f * channels));
        }
    } else if (fmt == 1 && bits == 32) {
        frames = data.size() / (4 * channels);
        for (size_t i = 0; i < frames; i++) {
            int64_t acc = 0;
            for (int c = 0; c < channels; c++, p += 4) {
                int32_t v = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                                    (uint32_t(p[2]) << 16) |
                                    (uint32_t(p[3]) << 24));
                acc += v;
            }
            emit(static_cast<float>(static_cast<double>(acc) /
                                    (2147483648.0 * channels)));
        }
    } else if (fmt == 3 && bits == 32) {
        frames = data.size() / (4 * channels);
        for (size_t i = 0; i < frames; i++) {
            float acc = 0;
            for (int c = 0; c < channels; c++, p += 4) {
                float v;
                std::memcpy(&v, p, 4);
                acc += v;
            }
            emit(acc / channels);
        }
    } else {
        throw std::runtime_error("unsupported WAV format (fmt=" +
                                 std::to_string(fmt) +
                                 " bits=" + std::to_string(bits) + ")");
    }
    return w;
}

void write_wav(const std::string& path, int sample_rate,
               const std::vector<float>& samples) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
    f.write("RIFF", 4);
    wr_u32(f, 36 + data_size);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    wr_u32(f, 16);
    wr_u16(f, 1);  // PCM
    wr_u16(f, 1);  // mono
    wr_u32(f, static_cast<uint32_t>(sample_rate));
    wr_u32(f, static_cast<uint32_t>(sample_rate * 2));
    wr_u16(f, 2);
    wr_u16(f, 16);
    f.write("data", 4);
    wr_u32(f, data_size);
    for (float v : samples) {
        float c = std::max(-1.0f, std::min(1.0f, v));
        wr_u16(f, static_cast<uint16_t>(
                      static_cast<int16_t>(c * 32767.0f)));
    }
}

}  // namespace nova
