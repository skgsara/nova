// image.hpp — grayscale image container + PGM writer.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nova {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> px;  // row-major, 0 = black, 255 = white
};

void write_pgm(const std::string& path, const Image& img);

}  // namespace nova
