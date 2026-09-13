#ifndef MATERIAL_DATA_HPP
#define MATERIAL_DATA_HPP

#include <array>
#include <cstddef>
#include <optional>

struct MaterialData {
    std::array<float, 4> base_color_{
        1.0F, 1.0F, 1.0F, 1.0F
    };

    float metallic_ = 0.0F;
    float roughness_ = 1.0F;
    std::array<float, 3> emissive_color_{0.0F, 0.0F, 0.0F};
    float normal_scale_ = 1.0F;
    float occlusion_strength_ = 1.0F;
    bool alpha_mask_ = false;
    float alpha_cutoff_ = 0.5F;

    std::optional<std::size_t> base_color_texture_;
    std::optional<std::size_t> metallic_roughness_texture_;
    std::optional<std::size_t> normal_texture_;
    std::optional<std::size_t> occlusion_texture_;
    std::optional<std::size_t> emissive_texture_;
};

#endif
