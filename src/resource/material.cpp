#include "resource/gpu/material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace {
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

    auto checked_textures(MaterialTextures textures) -> MaterialTextures {
        if (!textures.base_color.valid() ||
            !textures.metallic_roughness.valid() ||
            !textures.normal.valid() ||
            !textures.occlusion.valid() ||
            !textures.emissive.valid()) {
            throw std::invalid_argument(
                "material requires valid texture resource ids"
            );
        }
        return textures;
    }
}

auto make_gpu_material(
    const MaterialData& data,
    MaterialTextures textures
) -> GpuMaterial {
    textures = checked_textures(textures);
    const auto base_color = unit_color(data.base_color_);
    const auto emissive = sanitized_emissive_color(data.emissive_color_);

    return {
        .base_color_factor = base_color,
        .emissive_normal_scale = {
            emissive[0],
            emissive[1],
            emissive[2],
            finite_or(data.normal_scale_, 1.0F)
        },
        .metallic_roughness_occlusion_alpha_cutoff = {
            unit_value(data.metallic_, 0.0F),
            unit_value(data.roughness_, 1.0F),
            unit_value(data.occlusion_strength_, 1.0F),
            unit_value(data.alpha_cutoff_, 0.5F)
        },
        .flags = {
            data.alpha_mask_ ? 1U : 0U,
            0U,
            0U,
            0U
        },
        .base_color_texture_index = textures.base_color.value(),
        .metallic_roughness_texture_index = textures.metallic_roughness.value(),
        .normal_texture_index = textures.normal.value(),
        .occlusion_texture_index = textures.occlusion.value(),
        .emissive_texture_index = textures.emissive.value()
    };
}
