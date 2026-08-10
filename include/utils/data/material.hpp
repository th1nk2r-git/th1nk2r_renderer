#ifndef MATERIAL_DATA_HPP
#define MATERIAL_DATA_HPP

#include <array>
#include <optional>

#include "utils/data/image.hpp"

struct MaterialData {
    std::array<float, 4> base_color_{
        1.0F, 1.0F, 1.0F, 1.0F
    };

    std::optional<ImageData> base_color_texture_;
};

#endif