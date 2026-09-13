#include "render/pass/forward/material_writer.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/vec4.hpp>

#include "resource/gpu/material.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/registry/resource_registry.hpp"

namespace {
    constexpr uint32_t texture_count = 5;

    struct alignas(16) GpuMaterialParameters {
        glm::vec4 base_color_factor{1.0F};
        glm::vec4 emissive_normal_scale{0.0F, 0.0F, 0.0F, 1.0F};
        glm::vec4 metallic_roughness_occlusion_alpha_cutoff{
            0.0F, 1.0F, 1.0F, 0.5F
        };
        glm::uvec4 flags{0U};
    };

    static_assert(sizeof(GpuMaterialParameters) == 64);

    auto checked_material_capacity(uint32_t max_material_count) -> uint32_t {
        if (max_material_count == 0) {
            throw std::invalid_argument(
                "material writer requires a non-zero material capacity"
            );
        }
        if (max_material_count >
            std::numeric_limits<uint32_t>::max() / texture_count) {
            throw std::overflow_error(
                "material descriptor count exceeds uint32_t"
            );
        }
        return max_material_count;
    }

    auto align_up(
        vk::DeviceSize value,
        vk::DeviceSize alignment
    ) -> vk::DeviceSize {
        if (alignment <= 1) {
            return value;
        }
        if (value >
            std::numeric_limits<vk::DeviceSize>::max() - alignment + 1) {
            throw std::overflow_error("material buffer alignment overflow");
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    auto parameter_stride(const Device& device) -> vk::DeviceSize {
        const auto limits = device.physical_device().getProperties().limits;
        if (sizeof(GpuMaterialParameters) > limits.maxUniformBufferRange) {
            throw std::runtime_error(
                "material parameters exceed maxUniformBufferRange"
            );
        }
        return align_up(
            sizeof(GpuMaterialParameters),
            limits.minUniformBufferOffsetAlignment
        );
    }

    auto parameter_buffer_size(
        vk::DeviceSize stride,
        uint32_t max_material_count
    ) -> vk::DeviceSize {
        if (stride > std::numeric_limits<vk::DeviceSize>::max() /
            max_material_count) {
            throw std::overflow_error("material parameter buffer overflow");
        }
        return stride * max_material_count;
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t max_material_count
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                max_material_count * texture_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                max_material_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer,
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

    auto create_sampler(const Device& device) -> vk::raii::Sampler {
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
            .setMaxLod(std::numeric_limits<float>::max())
            .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }

    auto texture_info(const Texture& texture) -> vk::DescriptorImageInfo {
        return vk::DescriptorImageInfo{
            .imageView = *texture.image_view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
    }
}

MaterialWriter::MaterialWriter(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::DescriptorSetLayout& descriptor_set_layout,
    uint32_t max_material_count
) : device_(device),
    descriptor_set_layout_(descriptor_set_layout),
    max_material_count_(checked_material_capacity(max_material_count)),
    parameter_stride_(parameter_stride(device)),
    parameter_buffer_(
        allocator,
        BufferDesc{
            .size = parameter_buffer_size(
                parameter_stride_,
                max_material_count_
            ),
            .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }
    ),
    descriptor_pool_(create_descriptor_pool(device, max_material_count_)) {}

auto MaterialWriter::write(
    ResourceId<Material> id,
    const Material& material,
    const ResourceRegistry& registry
) -> void {
    if (!id.valid()) {
        throw std::invalid_argument("material resource id is invalid");
    }
    if (bindings_.contains(id.value())) {
        throw std::logic_error(
            "material descriptors are already written for this resource"
        );
    }
    if (next_material_index_ >= max_material_count_) {
        throw std::length_error(
            "material count exceeds material writer capacity"
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
    auto sampler = create_sampler(device_);

    const auto& base_color_factor = material.base_color_factor();
    const auto& emissive_color = material.emissive_color();
    const GpuMaterialParameters parameters{
        .base_color_factor = glm::vec4{
            base_color_factor[0],
            base_color_factor[1],
            base_color_factor[2],
            base_color_factor[3]
        },
        .emissive_normal_scale = glm::vec4{
            emissive_color[0],
            emissive_color[1],
            emissive_color[2],
            material.normal_scale()
        },
        .metallic_roughness_occlusion_alpha_cutoff = glm::vec4{
            material.metallic(),
            material.roughness(),
            material.occlusion_strength(),
            material.alpha_cutoff()
        },
        .flags = glm::uvec4{material.alpha_mask() ? 1U : 0U, 0U, 0U, 0U}
    };
    const auto parameter_offset =
        parameter_stride_ * next_material_index_;
    parameter_buffer_.write(
        &parameters,
        sizeof(parameters),
        parameter_offset
    );

    const vk::DescriptorImageInfo sampler_info{.sampler = *sampler};
    const std::array texture_infos{
        texture_info(registry.query(material.base_color_texture_id())),
        texture_info(registry.query(material.metallic_roughness_texture_id())),
        texture_info(registry.query(material.normal_texture_id())),
        texture_info(registry.query(material.occlusion_texture_id())),
        texture_info(registry.query(material.emissive_texture_id()))
    };
    const vk::DescriptorBufferInfo parameter_info{
        .buffer = parameter_buffer_.get(),
        .offset = parameter_offset,
        .range = sizeof(GpuMaterialParameters)
    };

    std::array<vk::WriteDescriptorSet, 7> writes{};
    writes[0]
        .setDstSet(*descriptor_set)
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eSampler)
        .setImageInfo(sampler_info);
    for (uint32_t index = 0; index < texture_count; ++index) {
        writes[index + 1]
            .setDstSet(*descriptor_set)
            .setDstBinding(index + 1)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setImageInfo(texture_infos[index]);
    }
    writes[6]
        .setDstSet(*descriptor_set)
        .setDstBinding(6)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setBufferInfo(parameter_info);
    device_.logical_device().updateDescriptorSets(writes, {});
    bindings_.try_emplace(
        id.value(),
        MaterialBinding{
            .sampler = std::move(sampler),
            .descriptor_set = std::move(descriptor_set)
        }
    );
    ++next_material_index_;
}
