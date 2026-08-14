// png.hpp — the greyscale PNG writer [DECIDED session 16, Sara; ROADMAP
// M4]: hand-rolled, uncompressed deflate, no new dependency. Nova has no
// external dependencies and libpng/zlib would change that; a PNG with
// stored (uncompressed) deflate blocks is a checksummed container around
// the raw scanlines, small enough to write correctly and — the point of
// the png_roundtrip screamer — small enough to verify against an
// independent decoder.
//
// Why PNG at all, and why the tEXt support: the saved image is the
// product [docs/05 §8.3 item 7 — "format stays greyscale PNG only"], and
// the metadata chunks are the home for Nova's decode QA, with precedent
// in the Furunos' printed Phase OK/NG header.
#pragma once

#include "../core/image.hpp"

#include <string>
#include <vector>

namespace nova {

// One tEXt chunk: keyword (1-79 bytes, no NUL) and latin-1 text. The
// writer does not escape or translate; callers keep to printable text.
struct PngText {
    std::string key;
    std::string value;
};

// Writes `img` as an 8-bit greyscale PNG (color type 0, no interlace,
// filter type 0 on every row). Throws std::runtime_error on an
// unwritable path, invalid_argument on an empty image — a 0-height PNG
// is not a file any decoder owes us.
void write_png(const std::string& path, const Image& img,
               const std::vector<PngText>& text = {});

}  // namespace nova
