// png.cpp — see png.hpp. The format: PNG signature; IHDR (8-bit
// greyscale); one tEXt chunk per metadata entry; one IDAT holding a zlib
// stream whose deflate payload is STORED blocks (no compression — the
// decision is "hand-rolled, no dependency", and a compressor is the part
// that would be wrong to hand-roll); IEND. Every chunk CRC'd, the zlib
// stream adler32'd.
#include "png.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace nova {
namespace {

uint32_t crc32_png(const uint8_t* p, std::size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32_zlib(const uint8_t* p, std::size_t n) {
    uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// One chunk: BE32 length, 4-byte type, payload, CRC over type+payload.
void put_chunk(std::vector<uint8_t>& out, const char type[4],
               const std::vector<uint8_t>& payload) {
    put_be32(out, static_cast<uint32_t>(payload.size()));
    const std::size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    put_be32(out, crc32_png(out.data() + at, 4 + payload.size()));
}

}  // namespace

void write_png(const std::string& path, const Image& img,
               const std::vector<PngText>& text) {
    if (img.width <= 0 || img.height <= 0 ||
        img.px.size() < static_cast<std::size_t>(img.width) * img.height)
        throw std::invalid_argument("write_png: empty or short image");

    std::vector<uint8_t> out;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(img.width));
    put_be32(ihdr, static_cast<uint32_t>(img.height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(0);  // color type: greyscale
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // no interlace
    put_chunk(out, "IHDR", ihdr);

    for (const PngText& t : text) {
        if (t.key.empty() || t.key.size() > 79)
            throw std::invalid_argument("write_png: bad tEXt keyword");
        std::vector<uint8_t> payload(t.key.begin(), t.key.end());
        payload.push_back(0);
        payload.insert(payload.end(), t.value.begin(), t.value.end());
        put_chunk(out, "tEXt", payload);
    }

    // The raw scanlines, one filter-0 byte per row.
    const std::size_t row = static_cast<std::size_t>(img.width) + 1;
    const std::size_t raw_n = row * static_cast<std::size_t>(img.height);
    std::vector<uint8_t> raw(raw_n);
    for (int y = 0; y < img.height; y++) {
        raw[static_cast<std::size_t>(y) * row] = 0;  // filter: none
        std::copy(img.px.begin() + y * img.width,
                  img.px.begin() + (y + 1) * img.width,
                  raw.begin() + static_cast<std::size_t>(y) * row + 1);
    }

    // The zlib stream: 0x78 0x01, then deflate STORED blocks of at most
    // 65535 bytes (LEN/NLEN little-endian, BFINAL on the last), then the
    // adler32 of the raw data, big-endian.
    std::vector<uint8_t> idat;
    idat.push_back(0x78);
    idat.push_back(0x01);
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw_n - pos);
        idat.push_back(pos + n == raw_n ? 0x01 : 0x00);
        idat.push_back(static_cast<uint8_t>(n & 0xFF));
        idat.push_back(static_cast<uint8_t>(n >> 8));
        const uint16_t nlen = static_cast<uint16_t>(~n);
        idat.push_back(static_cast<uint8_t>(nlen & 0xFF));
        idat.push_back(static_cast<uint8_t>(nlen >> 8));
        idat.insert(idat.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                    raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw_n);
    put_be32(idat, adler32_zlib(raw.data(), raw.size()));
    put_chunk(out, "IDAT", idat);

    put_chunk(out, "IEND", {});

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    const std::size_t wrote = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (wrote != out.size())
        throw std::runtime_error("short write to " + path);
}

}  // namespace nova
