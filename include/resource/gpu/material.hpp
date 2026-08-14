#ifndef MATERIAL_HPP
#define MATERIAL_HPP

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

    auto metallic_roughness_texture() const noexcept -> const Texture* {
        return metallic_roughness_texture_.get();
    }

    auto normal_texture() const noexcept -> const Texture* {
        return normal_texture_.get();
    }

    auto occlusion_texture() const noexcept -> const Texture* {
        return occlusion_texture_.get();
    }

    auto emissive_texture() const noexcept -> const Texture* {
        return emissive_texture_.get();
    }

private:
    std::unique_ptr<Texture> base_color_texture_;
    std::unique_ptr<Texture> metallic_roughness_texture_;
    std::unique_ptr<Texture> normal_texture_;
    std::unique_ptr<Texture> occlusion_texture_;
    std::unique_ptr<Texture> emissive_texture_;
};

#endif
