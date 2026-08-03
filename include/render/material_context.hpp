#ifndef MATERIAL_CONTEXT_HPP
#define MATERIAL_CONTEXT_HPP

#include "gfx/descriptor/descriptor_pool.hpp"
#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/resource/sampler.hpp"
#include "gfx/resource/texture2d.hpp"

class MaterialContext {
public:
    MaterialContext() = default;

    MaterialContext(
        const Device& device,
        DescriptorPool& descriptor_pool,
        const DescriptorSetLayout& descriptor_set_layout,
        const Texture2D& texture,
        const Sampler& sampler
    );

    MaterialContext(const MaterialContext&) = delete;
    auto operator=(const MaterialContext&) -> MaterialContext& = delete;

    MaterialContext(MaterialContext&&) noexcept = default;
    auto operator=(MaterialContext&&) noexcept -> MaterialContext& = default;

    auto descriptor_set() const noexcept
        -> const vk::raii::DescriptorSet& {
        return descriptor_set_;
    }

private:
    vk::raii::DescriptorSet descriptor_set_ = nullptr;
};

#endif
