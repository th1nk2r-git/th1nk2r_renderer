#include "resource/gpu/material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    auto color_byte(float value) noexcept -> std::byte {
        if (!std::isfinite(value)) {
            value = 0.0F;
        }
        const auto scaled = std::clamp(value, 0.0F, 1.0F) * 255.0F;
        return static_cast<std::byte>(
            static_cast<uint8_t>(std::lround(scaled))
        );
    }

    auto create_texture(
        const std::optional<ImageData>& image_data,
        const Device& device,
        const MemoryAllocator& allocator,
        ImageUploader& uploader,
        vk::Format format,
        std::string_view role
    ) -> std::unique_ptr<Texture> {
        if (!image_data) {
            return nullptr;
        }

        const auto& image = *image_data;
        if (image.channels != 4) {
            throw std::invalid_argument(
                "material " + std::string{role} +
                " texture must contain RGBA8 pixels"
            );
        }
        return std::make_unique<Texture>(
            device,
            allocator,
            uploader,
            image.width,
            image.height,
            image.pixels,
            format
        );
    }

    auto create_base_color_texture(
        const MaterialData& data,
        const Device& device,
        const MemoryAllocator& allocator,
        ImageUploader& uploader
    ) -> std::unique_ptr<Texture> {
        if (auto texture = create_texture(
                data.base_color_texture_,
                device,
                allocator,
                uploader,
                vk::Format::eR8G8B8A8Srgb,
                "base color"
            )) {
            return texture;
        }

        const std::array pixels{
            color_byte(data.base_color_[0]),
            color_byte(data.base_color_[1]),
            color_byte(data.base_color_[2]),
            color_byte(data.base_color_[3])
        };
        return std::make_unique<Texture>(
            device,
            allocator,
            uploader,
            1,
            1,
            pixels,
            vk::Format::eR8G8B8A8Srgb
        );
    }

}

Material::Material(
    const MaterialData& data,
    const Device& device,
    const MemoryAllocator& allocator,
    ImageUploader& uploader
) : base_color_texture_(
        create_base_color_texture(
            data,
            device,
            allocator,
            uploader
        )
    ),
    metallic_roughness_texture_(
        create_texture(
            data.metallic_roughness_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "metallic-roughness"
        )
    ),
    normal_texture_(
        create_texture(
            data.normal_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "normal"
        )
    ),
    occlusion_texture_(
        create_texture(
            data.occlusion_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "occlusion"
        )
    ),
    emissive_texture_(
        create_texture(
            data.emissive_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Srgb,
            "emissive"
        )
    ) {}
