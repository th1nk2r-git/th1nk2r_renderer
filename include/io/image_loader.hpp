#ifndef IMAGE_LOADER_HPP
#define IMAGE_LOADER_HPP

#include "resource/image_data.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

auto load_image_rgba8(const std::filesystem::path& path) -> ImageData;

auto load_image_rgba8(std::span<const std::byte> encoded_data) -> ImageData;

#endif