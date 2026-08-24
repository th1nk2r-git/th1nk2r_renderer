#include "render/pass/shadow/shadow_material_writer.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/vec4.hpp>

#include "resource/gpu/material.hpp"
#include "resource/gpu/texture.hpp"

namespace {
    struct alignas(16) GpuShadowMaterialParameters {
        glm::vec4 alpha{1.0F, 0.5F, 0.0F, 0.0F};
    };

    static_assert(sizeof(GpuShadowMaterialParameters) == 16);

    auto checked_material_capacity(uint32_t max_material_count) -> uint32_t {
        if (max_material_count == 0) {
            throw std::invalid_argument(
                "shadow material writer requires a non-zero capacity"
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
            throw std::overflow_error(
                "shadow material parameter alignment overflow"
            );
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    auto parameter_stride(const Device& device) -> vk::DeviceSize {
        const auto limits = device.physical_device().getProperties().limits;
        return align_up(
            sizeof(GpuShadowMaterialParameters),
            limits.minUniformBufferOffsetAlignment
        );
    }

    auto parameter_buffer_size(
        vk::DeviceSize stride,
        uint32_t max_material_count
    ) -> vk::DeviceSize {
        if (stride > std::numeric_limits<vk::DeviceSize>::max() /
            max_material_count) {
            throw std::overflow_error(
                "shadow material parameter buffer overflow"
            );
        }
        return stride * max_material_count;
    }

    auto create_descriptor_set_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            }
        };
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t max_material_count
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                max_material_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
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
            .setMipmapMode(vk::SamplerMipmapMode::eNearest)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setAnisotropyEnable(false)
            .setCompareEnable(false)
            .setMinLod(0.0F)
            .setMaxLod(0.0F)
            .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }
}

ShadowMaterialWriter::ShadowMaterialWriter(
    const Device& device,
    const MemoryAllocator& allocator,
    uint32_t max_material_count
) : device_(device),
    max_material_count_(checked_material_capacity(max_material_count)),
    parameter_stride_(parameter_stride(device)),
    descriptor_set_layout_(create_descriptor_set_layout(device)),
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

auto ShadowMaterialWriter::write(
    ResourceId<Material> id,
    const Material& material
) -> void {
    if (!id.valid()) {
        throw std::invalid_argument(
            "shadow material resource id is invalid"
        );
    }
    if (bindings_.contains(id.value())) {
        throw std::logic_error(
            "shadow material descriptors are already written"
        );
    }
    if (next_material_index_ >= max_material_count_) {
        throw std::length_error(
            "shadow material count exceeds writer capacity"
        );
    }

    const std::array layouts{*descriptor_set_layout_};
    vk::DescriptorSetAllocateInfo allocate_info{};
    allocate_info
        .setDescriptorPool(*descriptor_pool_)
        .setSetLayouts(layouts);
    auto descriptor_sets =
        device_.logical_device().allocateDescriptorSets(allocate_info);
    auto descriptor_set = std::move(descriptor_sets.front());
    auto sampler = create_sampler(device_);

    const auto parameters = GpuShadowMaterialParameters{
        .alpha = glm::vec4{
            material.base_color_factor()[3],
            material.alpha_cutoff(),
            material.alpha_mask() ? 1.0F : 0.0F,
            0.0F
        }
    };
    const auto parameter_offset =
        parameter_stride_ * next_material_index_;
    parameter_buffer_.write(
        &parameters,
        sizeof(parameters),
        parameter_offset
    );

    const vk::DescriptorImageInfo sampler_info{.sampler = *sampler};
    const vk::DescriptorImageInfo texture_info{
        .imageView = *material.base_color_texture().image_view(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    const vk::DescriptorBufferInfo parameter_info{
        .buffer = parameter_buffer_.get(),
        .offset = parameter_offset,
        .range = sizeof(parameters)
    };
    const std::array writes{
        vk::WriteDescriptorSet{}
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info),
        vk::WriteDescriptorSet{}
            .setDstSet(*descriptor_set)
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setImageInfo(texture_info),
        vk::WriteDescriptorSet{}
            .setDstSet(*descriptor_set)
            .setDstBinding(2)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(parameter_info)
    };
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
