#ifndef TEXTURE2D_HPP
#define TEXTURE2D_HPP

#include "gfx/device/device.hpp"
#include "gfx/device/data_uploader.hpp"
#include "gfx/device/gpu_allocator.hpp"
#include "gfx/resource/image.hpp"
#include "gfx/resource/image_view.hpp"

class Texture2D {
public:
    Texture2D(
        const Device& device,
        const GpuAllocator& allocator,
        DataUploader& uploader,
        uint32_t width,
        uint32_t height,
        std::span<const std::byte> pixels,
        vk::Format format
    );

    Texture2D(const Texture2D&) = delete;
    auto operator=(const Texture2D&) -> Texture2D& = delete;
    Texture2D(Texture2D&&) = delete;
    auto operator=(Texture2D&&) -> Texture2D& = delete;

    auto image() const noexcept -> const Image& {
        return image_;
    }

    auto image_view() const noexcept -> const ImageView& {
        return image_view_;
    }

private:
    Image image_;
    ImageView image_view_;
};

#endif
