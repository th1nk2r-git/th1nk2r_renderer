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

Material::Material(
    const MaterialData& data,
    MaterialTextures textures
) : textures_(checked_textures(textures)),
    base_color_factor_(unit_color(data.base_color_)),
    metallic_(unit_value(data.metallic_, 0.0F)),
    roughness_(unit_value(data.roughness_, 1.0F)),
    emissive_color_(sanitized_emissive_color(data.emissive_color_)),
    normal_scale_(finite_or(data.normal_scale_, 1.0F)),
    occlusion_strength_(unit_value(data.occlusion_strength_, 1.0F)),
    alpha_mask_(data.alpha_mask_),
    alpha_cutoff_(unit_value(data.alpha_cutoff_, 0.5F)) {}
