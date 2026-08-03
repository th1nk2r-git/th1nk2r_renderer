#ifndef IMAGE_LOADER_HPP
#define IMAGE_LOADER_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    std::vector<std::byte> pixels;
};

auto load_image_rgba8(const std::filesystem::path& path) -> ImageData;

auto load_image_rgba8(std::span<const std::byte> encoded_data) -> ImageData;

#endif