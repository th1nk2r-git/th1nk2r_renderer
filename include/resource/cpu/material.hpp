#ifndef MATERIAL_DATA_HPP
#define MATERIAL_DATA_HPP

#include <array>
#include <optional>

#include "resource/cpu/image.hpp"

struct MaterialData {
    std::array<float, 4> base_color_{
        1.0F, 1.0F, 1.0F, 1.0F
    };

    std::optional<ImageData> base_color_texture_;
    std::optional<ImageData> metallic_roughness_texture_;
    std::optional<ImageData> normal_texture_;
    std::optional<ImageData> occlusion_texture_;
    std::optional<ImageData> emissive_texture_;
};

#endif
