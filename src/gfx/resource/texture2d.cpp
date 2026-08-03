#include "gfx/resource/texture2d.hpp"

#include <limits>
#include <stdexcept>

namespace {
    constexpr size_t rgba8_texel_size = 4;

    auto validate_format(vk::Format format) -> void {
        if (format != vk::Format::eR8G8B8A8Unorm &&
            format != vk::Format::eR8G8B8A8Srgb) {
            throw std::invalid_argument(
                "Texture2D currently requires an RGBA8 UNORM or SRGB format!"
            );
        }
    }

    auto expected_pixel_size(uint32_t width, uint32_t height) -> size_t {
        if (width == 0 || height == 0) {
            throw std::invalid_argument(
                "Texture2D dimensions must be greater than zero!"
            );
        }

        constexpr auto max_size = std::numeric_limits<size_t>::max();
        const auto image_width = static_cast<size_t>(width);
        const auto image_height = static_cast<size_t>(height);

        if (image_width > max_size / image_height) {
            throw std::length_error(
                "Texture2D pixel count exceeds size_t!"
            );
        }

        const auto texel_count = image_width * image_height;
        if (texel_count > max_size / rgba8_texel_size) {
            throw std::length_error(
                "Texture2D pixel byte size exceeds size_t!"
            );
        }

        return texel_count * rgba8_texel_size;
    }

    auto validate_device_support(
        const Device& device,
        uint32_t width,
        uint32_t height,
        vk::Format format
    ) -> void {
        const auto properties = device.physical_device().getProperties();
        if (width > properties.limits.maxImageDimension2D ||
            height > properties.limits.maxImageDimension2D) {
            throw std::out_of_range(
                "Texture2D dimensions exceed the device limit!"
            );
        }

        const auto format_properties =
            device.physical_device().getFormatProperties(format);
        const auto required_features =
            vk::FormatFeatureFlagBits::eSampledImage |
            vk::FormatFeatureFlagBits::eTransferDst;

        if ((format_properties.optimalTilingFeatures & required_features) !=
            required_features) {
            throw std::runtime_error(
                "Texture2D format does not support sampling and transfer-dst!"
            );
        }
    }

    auto create_image_desc(
        const Device& device,
        uint32_t width,
        uint32_t height,
        std::span<const std::byte> pixels,
        vk::Format format
    ) -> ImageDesc {
        validate_format(format);

        const auto required_size = expected_pixel_size(width, height);
        if (pixels.size() != required_size) {
            throw std::invalid_argument(
                "Texture2D pixel data size does not match its dimensions!"
            );
        }

        validate_device_support(device, width, height, format);

        return ImageDesc{
            .format = format,
            .extent = vk::Extent3D{width, height, 1},
            .mip_levels = 1,
            .array_layers = 1,
            .usage = vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled
        };
    }
}

Texture2D::Texture2D(
    const Device& device,
    const GpuAllocator& allocator,
    DataUploader& uploader,
    uint32_t width,
    uint32_t height,
    std::span<const std::byte> pixels,
    vk::Format format
)
    : image_(
          allocator,
          create_image_desc(
              device,
              width,
              height,
              pixels,
              format
          )
      ) {
    ImageUploadDesc upload_desc{};
    upload_desc.extent = vk::Extent3D{width, height, 1};

    uploader.enqueue_image(
        pixels,
        image_,
        upload_desc
    );
    uploader.submit_and_wait();

    image_view_ = ImageView(
        device,
        ImageViewDesc{
            .image = image_.get(),
            .format = format,
            .aspect_flags = vk::ImageAspectFlagBits::eColor,
            .view_type = vk::ImageViewType::e2D,
            .base_mip_level = 0,
            .level_count = 1,
            .base_array_layer = 0,
            .layer_count = 1
        }
    );
}
