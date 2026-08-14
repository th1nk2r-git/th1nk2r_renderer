#include "render/pass/forward/material_writer.hpp"

#include <array>
#include <stdexcept>
#include <utility>

#include "resource/gpu/material.hpp"
#include "resource/gpu/texture.hpp"

namespace {
    auto create_descriptor_pool(
        const Device& device,
        uint32_t max_material_count
    ) -> vk::raii::DescriptorPool {
        if (max_material_count == 0) {
            throw std::invalid_argument(
                "material writer requires a non-zero material capacity"
            );
        }

        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                max_material_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                max_material_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(max_material_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_sampler(
        const Device& device,
        const Texture& texture
    ) -> vk::raii::Sampler {
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setMipLodBias(0.0F)
            .setAnisotropyEnable(false)
            .setMaxAnisotropy(1.0F)
            .setCompareEnable(false)
            .setCompareOp(vk::CompareOp::eNever)
            .setMinLod(0.0F)
            .setMaxLod(static_cast<float>(texture.image().mip_levels() - 1))
            .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }
}

MaterialWriter::MaterialWriter(
    const Device& device,
    const vk::raii::DescriptorSetLayout& descriptor_set_layout,
    uint32_t max_material_count
) : device_(device),
    descriptor_set_layout_(descriptor_set_layout),
    descriptor_pool_(create_descriptor_pool(device, max_material_count)) {}

auto MaterialWriter::write(
    ResourceId<Material> id,
    const Material& material
) -> void {
    if (!id.valid()) {
        throw std::invalid_argument("material resource id is invalid");
    }
    if (bindings_.contains(id.value())) {
        throw std::logic_error(
            "material descriptors are already written for this resource"
        );
    }

    const std::array layouts{*descriptor_set_layout_};
    vk::DescriptorSetAllocateInfo allocate_info{};
    allocate_info
        .setDescriptorPool(*descriptor_pool_)
        .setSetLayouts(layouts);
    auto descriptor_sets =
        device_.logical_device().allocateDescriptorSets(allocate_info);
    if (descriptor_sets.size() != 1) {
        throw std::runtime_error(
            "material descriptor allocation returned an unexpected count"
        );
    }

    auto descriptor_set = std::move(descriptor_sets.front());
    auto sampler = create_sampler(device_, material.base_color_texture());
    const vk::DescriptorImageInfo sampler_info{.sampler = *sampler};
    const vk::DescriptorImageInfo texture_info{
        .imageView = *material.base_color_texture().image_view(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0]
        .setDstSet(*descriptor_set)
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eSampler)
        .setImageInfo(sampler_info);
    writes[1]
        .setDstSet(*descriptor_set)
        .setDstBinding(1)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eSampledImage)
        .setImageInfo(texture_info);
    device_.logical_device().updateDescriptorSets(writes, {});
    bindings_.try_emplace(
        id.value(),
        MaterialBinding{
            .sampler = std::move(sampler),
            .descriptor_set = std::move(descriptor_set)
        }
    );
}
