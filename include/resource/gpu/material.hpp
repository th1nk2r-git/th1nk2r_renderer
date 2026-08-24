#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <array>
#include <memory>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "resource/cpu/material.hpp"
#include "resource/gpu/texture.hpp"

class Material {
public:
    Material(
        const MaterialData& data,
        const Device& device,
        const MemoryAllocator& allocator,
        ImageUploader& uploader
    );

    auto base_color_texture() const noexcept -> const Texture& {
        return *base_color_texture_;
    }

    auto metallic_roughness_texture() const noexcept -> const Texture& {
        return *metallic_roughness_texture_;
    }

    auto normal_texture() const noexcept -> const Texture& {
        return *normal_texture_;
    }

    auto occlusion_texture() const noexcept -> const Texture& {
        return *occlusion_texture_;
    }

    auto emissive_texture() const noexcept -> const Texture& {
        return *emissive_texture_;
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
    std::unique_ptr<Texture> base_color_texture_;
    std::unique_ptr<Texture> metallic_roughness_texture_;
    std::unique_ptr<Texture> normal_texture_;
    std::unique_ptr<Texture> occlusion_texture_;
    std::unique_ptr<Texture> emissive_texture_;
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
