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

// One PCM sample format on disk: bytes per sample, the accumulator a
// frame's channels are summed into, and how one sample is read and the
// frame sum normalized to [-1, 1]. 24-bit has no native integer type, so
// it gets a tag; the float format (fmt == 3) reads through memcpy.
template <typename Sample> struct pcm_traits;

template <> struct pcm_traits<int16_t> {
    using acc_t = int32_t;
    static constexpr size_t bytes = 2;
    static int32_t read(const unsigned char*& p) {
        const int16_t v = static_cast<int16_t>(p[0] | (p[1] << 8));
        p += 2;
        return v;
    }
    static float finish(acc_t acc, int channels) {
        return static_cast<float>(acc) / (32768.0f * channels);
    }
};

template <> struct pcm_traits<uint8_t> {
    using acc_t = int32_t;
    static constexpr size_t bytes = 1;
    static int32_t read(const unsigned char*& p) { return int(*p++) - 128; }
    static float finish(acc_t acc, int channels) {
        return static_cast<float>(acc) / (128.0f * channels);
    }
};

struct pcm24 {};

template <> struct pcm_traits<pcm24> {
    using acc_t = int32_t;
    static constexpr size_t bytes = 3;
    static int32_t read(const unsigned char*& p) {
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) |
                    (int32_t(p[2]) << 16);
        if (v & 0x800000) v -= 0x1000000;
        p += 3;
        return v;
    }
    static float finish(acc_t acc, int channels) {
        return static_cast<float>(acc) / (8388608.0f * channels);
    }
};

template <> struct pcm_traits<int32_t> {
    using acc_t = int64_t;
    static constexpr size_t bytes = 4;
    static int32_t read(const unsigned char*& p) {
        const int32_t v = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                                  (uint32_t(p[2]) << 16) |
                                  (uint32_t(p[3]) << 24));
        p += 4;
        return v;
    }
    static float finish(acc_t acc, int channels) {
        return static_cast<float>(static_cast<double>(acc) /
                                  (2147483648.0 * channels));
    }
};

template <> struct pcm_traits<float> {
    using acc_t = float;
    static constexpr size_t bytes = 4;
    static float read(const unsigned char*& p) {
        float v;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    static float finish(acc_t acc, int channels) { return acc / channels; }
};

// Decode every frame of one PCM format: sum the frame's channels and push
// the normalized mean.
template <typename Sample>
void decode_frames(const std::vector<unsigned char>& data, int channels,
                   Wav& w) {
    using T = pcm_traits<Sample>;
    const unsigned char* p = data.data();
    const size_t frames = data.size() / (T::bytes * channels);
    for (size_t i = 0; i < frames; i++) {
        typename T::acc_t acc = 0;
        for (int c = 0; c < channels; c++) acc += T::read(p);
        w.samples.push_back(T::finish(acc, channels));
    }
}

}  // namespace

// Nyquist on the white frequency: WEFAX white is 2300 Hz [WMO §5.3.1.2],
// so a stream sampled at or below 4600 Hz cannot represent the signal at
// all and is not a recording of one. The upper bound is a sanity limit
// rather than a standards one — no capture device reaches it. Without the
// lower bound a declared rate of 1 Hz drives resample() to an 8000x
// output size [audit Pass D, D-PERF-002].
constexpr uint32_t kMinRateHz = 4600;
constexpr uint32_t kMaxRateHz = 768000;

Wav read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.seekg(0, std::ios::end);
    const std::streamoff file_bytes = f.tellg();
    f.seekg(0, std::ios::beg);
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
            // A chunk header is not evidence that its bytes exist. Clamp
            // the declared size to what is actually left in the file: a
            // 144-byte file declaring 4 GB otherwise allocates 4 GB and
            // zero-fills it, which is 144 bytes of input turning into
            // gigabytes of resident memory [audit Pass D, D-PERF-001].
            const std::streamoff here = f.tellg();
            const uint64_t left =
                (here < 0 || here > file_bytes) ? 0
                                                : uint64_t(file_bytes - here);
            const size_t want = static_cast<size_t>(std::min<uint64_t>(size, left));
            data.resize(want);
            f.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(want));
            have_data = true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);  // chunks are padded
        }
    }
    if (!have_fmt || !have_data)
        throw std::runtime_error("malformed WAV (missing fmt/data): " + path);
    if (channels < 1) throw std::runtime_error("WAV with 0 channels: " + path);
    if (rate < kMinRateHz || rate > kMaxRateHz)
        throw std::runtime_error(
            "implausible WAV sample rate (" + std::to_string(rate) +
            " Hz; WEFAX needs more than twice the 2300 Hz white tone): " +
            path);

    Wav w;
    w.sample_rate = static_cast<int>(rate);

    if (fmt == 1 && bits == 16)
        decode_frames<int16_t>(data, channels, w);
    else if (fmt == 1 && bits == 8)
        decode_frames<uint8_t>(data, channels, w);
    else if (fmt == 1 && bits == 24)
        decode_frames<pcm24>(data, channels, w);
    else if (fmt == 1 && bits == 32)
        decode_frames<int32_t>(data, channels, w);
    else if (fmt == 3 && bits == 32)
        decode_frames<float>(data, channels, w);
    else
        throw std::runtime_error("unsupported WAV format (fmt=" +
                                 std::to_string(fmt) +
                                 " bits=" + std::to_string(bits) + ")");
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
