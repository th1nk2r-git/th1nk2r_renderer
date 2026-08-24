#ifndef IMAGE_LOADER_HPP
#define IMAGE_LOADER_HPP

#include "resource/cpu/image.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

auto load_image_rgba8(const std::filesystem::path& path) -> ImageData;

auto load_image_rgba8(std::span<const std::byte> encoded_data) -> ImageData;

auto load_image_rgba32f(const std::filesystem::path& path) -> HdrImageData;

#endif
