#include "resource/gpu/material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    constexpr std::array white_texel{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}
    };
    constexpr std::array flat_normal_texel{
        std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}
    };

    auto finite_or(float value, float fallback) noexcept -> float {
        return std::isfinite(value) ? value : fallback;
    }

    auto unit_value(float value, float fallback) noexcept -> float {
        return std::clamp(finite_or(value, fallback), 0.0F, 1.0F);
    }

    auto unit_color(
        const std::array<float, 4>& color
    ) noexcept -> std::array<float, 4> {
        return {
            unit_value(color[0], 1.0F),
            unit_value(color[1], 1.0F),
            unit_value(color[2], 1.0F),
            unit_value(color[3], 1.0F)
        };
    }

    auto sanitized_emissive_color(
        const std::array<float, 3>& color
    ) noexcept -> std::array<float, 3> {
        return {
            std::max(finite_or(color[0], 0.0F), 0.0F),
            std::max(finite_or(color[1], 0.0F), 0.0F),
            std::max(finite_or(color[2], 0.0F), 0.0F)
        };
    }

    auto create_texture_or_fallback(
        const std::optional<ImageData>& image_data,
        const Device& device,
        const MemoryAllocator& allocator,
        ImageUploader& uploader,
        vk::Format format,
        std::string_view role,
        const std::array<std::byte, 4>& fallback_texel
    ) -> std::unique_ptr<Texture> {
        if (image_data) {
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

        return std::make_unique<Texture>(
            device,
            allocator,
            uploader,
            1,
            1,
            fallback_texel,
            format
        );
    }
}

Material::Material(
    const MaterialData& data,
    const Device& device,
    const MemoryAllocator& allocator,
    ImageUploader& uploader
) : base_color_texture_(
        create_texture_or_fallback(
            data.base_color_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Srgb,
            "base color",
            white_texel
        )
    ),
    metallic_roughness_texture_(
        create_texture_or_fallback(
            data.metallic_roughness_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "metallic-roughness",
            white_texel
        )
    ),
    normal_texture_(
        create_texture_or_fallback(
            data.normal_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "normal",
            flat_normal_texel
        )
    ),
    occlusion_texture_(
        create_texture_or_fallback(
            data.occlusion_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Unorm,
            "occlusion",
            white_texel
        )
    ),
    emissive_texture_(
        create_texture_or_fallback(
            data.emissive_texture_,
            device,
            allocator,
            uploader,
            vk::Format::eR8G8B8A8Srgb,
            "emissive",
            white_texel
        )
    ),
    base_color_factor_(unit_color(data.base_color_)),
    metallic_(unit_value(data.metallic_, 0.0F)),
    roughness_(unit_value(data.roughness_, 1.0F)),
    emissive_color_(sanitized_emissive_color(data.emissive_color_)),
    normal_scale_(finite_or(data.normal_scale_, 1.0F)),
    occlusion_strength_(unit_value(data.occlusion_strength_, 1.0F)),
    alpha_mask_(data.alpha_mask_),
    alpha_cutoff_(unit_value(data.alpha_cutoff_, 0.5F)) {}
