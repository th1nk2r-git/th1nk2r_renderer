#include "render/material_context.hpp"

#include <array>
#include <stdexcept>
#include <utility>

MaterialContext::MaterialContext(
    const Device& device,
    DescriptorPool& descriptor_pool,
    const DescriptorSetLayout& descriptor_set_layout,
    const Texture2D& texture,
    const Sampler& sampler
) {
    const std::array layouts{
        *descriptor_set_layout.get()
    };
    auto descriptor_sets = descriptor_pool.allocate_sets(
        device,
        layouts
    );

    if (descriptor_sets.size() != 1) {
        throw std::runtime_error(
            "material descriptor allocation returned an unexpected count!"
        );
    }

    descriptor_set_ = std::move(descriptor_sets.front());

    vk::DescriptorImageInfo texture_info{};
    texture_info
        .setImageView(*texture.image_view().get())
        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

    vk::DescriptorImageInfo sampler_info{};
    sampler_info.setSampler(*sampler.get());

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0]
        .setDstSet(*descriptor_set_)
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eSampledImage)
        .setImageInfo(texture_info);
    writes[1]
        .setDstSet(*descriptor_set_)
        .setDstBinding(1)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eSampler)
        .setImageInfo(sampler_info);

    device.logical_device().updateDescriptorSets(writes, {});
}
