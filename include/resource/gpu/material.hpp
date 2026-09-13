#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <array>

#include "resource/cpu/material.hpp"
#include "resource/gpu/resource_id.hpp"

class Texture;

struct MaterialTextures {
    ResourceId<Texture> base_color;
    ResourceId<Texture> metallic_roughness;
    ResourceId<Texture> normal;
    ResourceId<Texture> occlusion;
    ResourceId<Texture> emissive;
};

class Material {
public:
    Material(
        const MaterialData& data,
        MaterialTextures textures
    );

    auto base_color_texture_id() const noexcept -> ResourceId<Texture> {
        return textures_.base_color;
    }

    auto metallic_roughness_texture_id() const noexcept
        -> ResourceId<Texture> {
        return textures_.metallic_roughness;
    }

    auto normal_texture_id() const noexcept -> ResourceId<Texture> {
        return textures_.normal;
    }

    auto occlusion_texture_id() const noexcept -> ResourceId<Texture> {
        return textures_.occlusion;
    }

    auto emissive_texture_id() const noexcept -> ResourceId<Texture> {
        return textures_.emissive;
    }

    auto base_color_factor() const noexcept
        -> const std::array<float, 4>& {
        return base_color_factor_;
    }

    auto metallic() const noexcept -> float {
        return metallic_;
    }

    auto roughness() const noexcept -> float {
        return roughness_;
    }

    auto emissive_color() const noexcept -> const std::array<float, 3>& {
        return emissive_color_;
    }

    auto normal_scale() const noexcept -> float {
        return normal_scale_;
    }

    auto occlusion_strength() const noexcept -> float {
        return occlusion_strength_;
    }

    auto alpha_mask() const noexcept -> bool {
        return alpha_mask_;
    }

    auto alpha_cutoff() const noexcept -> float {
        return alpha_cutoff_;
    }

private:
    MaterialTextures textures_;
    std::array<float, 4> base_color_factor_{};
    float metallic_ = 0.0F;
    float roughness_ = 1.0F;
    std::array<float, 3> emissive_color_{};
    float normal_scale_ = 1.0F;
    float occlusion_strength_ = 1.0F;
    bool alpha_mask_ = false;
    float alpha_cutoff_ = 0.5F;
};

#endif
