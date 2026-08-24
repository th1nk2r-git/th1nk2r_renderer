#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS 16384

#include <stb_image.h>

#include "io/image_loader.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
    constexpr size_t rgba_channel_count = STBI_rgb_alpha;

    auto image_byte_size(int width, int height) -> size_t {
        if (width <= 0 || height <= 0) {
            throw std::runtime_error(
                "decoded image dimensions must be greater than zero"
            );
        }

        const auto image_width = static_cast<size_t>(width);
        const auto image_height = static_cast<size_t>(height);
        constexpr auto max_size = std::numeric_limits<size_t>::max();

        if (image_width > max_size / image_height) {
            throw std::length_error(
                "decoded image pixel count exceeds size_t"
            );
        }

        const auto pixel_count = image_width * image_height;
        if (pixel_count > max_size / rgba_channel_count) {
            throw std::length_error(
                "decoded image byte size exceeds size_t"
            );
        }

        return pixel_count * rgba_channel_count;
    }
}

auto load_image_rgba8(std::span<const std::byte> encoded_data) -> ImageData {
    if (encoded_data.empty()) {
        throw std::invalid_argument(
            "encoded image data cannot be empty"
        );
    }

    if (encoded_data.size() >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error(
            "encoded image data exceeds stb_image's input size limit"
        );
    }

    int width = 0;
    int height = 0;
    int source_channels = 0;

    auto* raw_pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(encoded_data.data()),
        static_cast<int>(encoded_data.size()),
        &width,
        &height,
        &source_channels,
        STBI_rgb_alpha
    );

    if (raw_pixels == nullptr) {
        const char* failure_reason = stbi_failure_reason();
        throw std::runtime_error(
            std::string{"failed to decode image: "} +
            (failure_reason != nullptr ? failure_reason : "unknown stb_image error")
        );
    }

    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
        raw_pixels,
        &stbi_image_free
    };

    const auto byte_size = image_byte_size(width, height);

    ImageData image_data{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .channels = static_cast<uint32_t>(rgba_channel_count),
        .pixels = std::vector<std::byte>(byte_size)
    };

    std::memcpy(
        image_data.pixels.data(),
        pixels.get(),
        byte_size
    );

    return image_data;
}

auto load_image_rgba8(const std::filesystem::path& path) -> ImageData {
    std::ifstream file{
        path,
        std::ios::binary | std::ios::ate
    };

    if (!file) {
        throw std::runtime_error(
            "failed to open image file: " + path.string()
        );
    }

    const auto end_position = file.tellg();
    if (end_position <= 0) {
        throw std::runtime_error(
            "image file is empty: " + path.string()
        );
    }

    const auto file_size = static_cast<std::streamoff>(end_position);
    if (static_cast<uintmax_t>(file_size) >
        static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
        file_size > std::numeric_limits<std::streamsize>::max() ||
        file_size > std::numeric_limits<int>::max()) {
        throw std::length_error(
            "image file is too large: " + path.string()
        );
    }

    std::vector<std::byte> encoded_data(
        static_cast<size_t>(file_size)
    );

    file.seekg(0, std::ios::beg);
    file.read(
        reinterpret_cast<char*>(encoded_data.data()),
        static_cast<std::streamsize>(file_size)
    );

    if (!file) {
        throw std::runtime_error(
            "failed to read image file: " + path.string()
        );
    }

    return load_image_rgba8(
        std::span<const std::byte>{encoded_data}
    );
}

auto load_image_rgba32f(const std::filesystem::path& path) -> HdrImageData {
    if (path.empty()) {
        throw std::invalid_argument("HDR image path cannot be empty");
    }

    const auto path_string = path.string();
    int width = 0;
    int height = 0;
    int source_channels = 0;
    auto* raw_pixels = stbi_loadf(
        path_string.c_str(),
        &width,
        &height,
        &source_channels,
        STBI_rgb_alpha
    );
    if (raw_pixels == nullptr) {
        const char* failure_reason = stbi_failure_reason();
        throw std::runtime_error(
            "failed to decode HDR image '" + path_string + "': " +
            (failure_reason != nullptr ?
                failure_reason : "unknown stb_image error")
        );
    }

    const std::unique_ptr<float, decltype(&stbi_image_free)> pixels{
        raw_pixels,
        &stbi_image_free
    };
    const auto element_count = image_byte_size(width, height);

    HdrImageData image_data{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .channels = static_cast<uint32_t>(rgba_channel_count),
        .pixels = std::vector<float>(element_count)
    };
    std::memcpy(
        image_data.pixels.data(),
        pixels.get(),
        element_count * sizeof(float)
    );
    return image_data;
}
