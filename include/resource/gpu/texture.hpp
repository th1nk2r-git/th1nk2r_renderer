#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/device/image_uploader.hpp"
#include "gfx/resource/image.hpp"

class Texture {
public:
    Texture(
        const Device& device,
        const MemoryAllocator& allocator,
        ImageUploader& uploader,
        uint32_t width,
        uint32_t height,
        std::span<const std::byte> pixels,
        vk::Format format
    );

    Texture(const Texture&) = delete;
    auto operator=(const Texture&) -> Texture& = delete;
    Texture(Texture&&) = delete;
    auto operator=(Texture&&) -> Texture& = delete;

    auto image() const noexcept -> const Image& {
        return image_;
    }

    auto image_view() const noexcept -> const vk::raii::ImageView& {
        return image_view_;
    }

private:
    Image image_;
    vk::raii::ImageView image_view_ = nullptr;
};

#endif
