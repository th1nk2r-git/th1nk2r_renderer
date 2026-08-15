#include "resource/gpu/texture.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace {
    constexpr size_t rgba8_texel_size = 4;

    auto validate_format(vk::Format format) -> void {
        if (format != vk::Format::eR8G8B8A8Unorm &&
            format != vk::Format::eR8G8B8A8Srgb) {
            throw std::invalid_argument(
                "Texture currently requires an RGBA8 UNORM or SRGB format!"
            );
        }
    }

    auto expected_pixel_size(uint32_t width, uint32_t height) -> size_t {
        if (width == 0 || height == 0) {
            throw std::invalid_argument(
                "Texture dimensions must be greater than zero!"
            );
        }

        constexpr auto max_size = std::numeric_limits<size_t>::max();
        const auto image_width = static_cast<size_t>(width);
        const auto image_height = static_cast<size_t>(height);

        if (image_width > max_size / image_height) {
            throw std::length_error(
                "Texture pixel count exceeds size_t!"
            );
        }

        const auto texel_count = image_width * image_height;
        if (texel_count > max_size / rgba8_texel_size) {
            throw std::length_error(
                "Texture pixel byte size exceeds size_t!"
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
                "Texture dimensions exceed the device limit!"
            );
        }

        const auto format_properties =
            device.physical_device().getFormatProperties(format);
        const auto required_features =
            vk::FormatFeatureFlagBits::eSampledImage;

        if ((format_properties.optimalTilingFeatures & required_features) !=
            required_features) {
            throw std::runtime_error(
                "Texture format does not support sampled images!"
            );
        }
    }


    auto calculate_mip_levels(uint32_t width, uint32_t height) -> uint32_t {
        return std::bit_width(std::max(width, height));
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
                "Texture pixel data size does not match its dimensions!"
            );
        }

        validate_device_support(device, width, height, format);

        return ImageDesc{
            .format = format,
            .extent = vk::Extent3D{width, height, 1},
            .mip_levels = calculate_mip_levels(width, height),
            .array_layers = 1,
            .usage = vk::ImageUsageFlagBits::eTransferSrc |
                     vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled
        };
    }

    auto create_image_view(
        const Device& device,
        const Image& image,
        vk::Format format
    ) -> vk::raii::ImageView {
        vk::ImageViewCreateInfo create_info{};
        create_info
            .setImage(image.get())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format)
            .setComponents(vk::ComponentMapping{
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity
            })
            .setSubresourceRange(vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor,
                0,
                image.mip_levels(),
                0,
                1
            });
        return device.logical_device().createImageView(create_info);
    }
}

Texture::Texture(
    const Device& device,
    const MemoryAllocator& allocator,
    ImageUploader& uploader,
    uint32_t width,
    uint32_t height,
    std::span<const std::byte> pixels,
    vk::Format format
) : image_(
          allocator,
          create_image_desc(
              device,
              width,
              height,
              pixels,
              format
          )
      ),
      image_view_(create_image_view(device, image_, format)) {
    ImageUploadDesc upload_desc{};
    upload_desc.extent = vk::Extent3D{width, height, 1};
    upload_desc.generate_mipmaps = image_.mip_levels() > 1;

    uploader.enqueue(
        pixels,
        image_,
        upload_desc
    );
}
