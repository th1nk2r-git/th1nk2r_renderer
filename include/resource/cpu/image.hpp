#ifndef IMAGE_DATA_HPP
#define IMAGE_DATA_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    std::vector<std::byte> pixels;
};

#endif
