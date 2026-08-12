// image.cpp
#include "image.hpp"
#include <cstdio>
#include <stdexcept>

namespace nova {

void write_pgm(const std::string& path, const Image& img) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "P5\n%d %d\n255\n", img.width, img.height);
    std::fwrite(img.px.data(), 1, img.px.size(), f);
    std::fclose(f);
}

}  // namespace nova
