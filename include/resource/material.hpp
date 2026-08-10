#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <memory>

#include "gfx/descriptor/descriptor_pool.hpp"
#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/device/device.hpp"
#include "gfx/resource/sampler.hpp"
#include "resource/texture.hpp"
#include "utils/data/material.hpp"

class Material {
public:
    Material(
        const MaterialData& data,
        const Device& device,
        const GpuAllocator& allocator,
        DataUploader& uploader,
        DescriptorPool& descriptor_pool,
        const DescriptorSetLayout& descriptor_set_layout
    );

    auto descriptor_set() const noexcept -> const vk::raii::DescriptorSet& {
        return descriptor_set_;
    }

    auto base_color_texture() const noexcept -> const Texture& {
        return *base_color_texture_;
    }

private:
    std::unique_ptr<Texture> base_color_texture_;
    Sampler sampler_;
    vk::raii::DescriptorSet descriptor_set_ = nullptr;
};

#endif
