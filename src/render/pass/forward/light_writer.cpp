#include "render/pass/forward/light_writer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/vec4.hpp>

#include "render/pass/shadow/shadow_pass.hpp"

namespace {
    struct alignas(16) GpuPointLight {
        glm::vec4 position_intensity{0.0F};
        glm::vec4 color{0.0F};
        glm::vec4 shadow_parameters{-1.0F, 25.0F, 0.15F, 0.1F};
    };

    static_assert(sizeof(GpuPointLight) == 48);

    auto checked_frame_count(uint32_t frame_count) -> uint32_t {
        if (frame_count == 0) {
            throw std::invalid_argument(
                "light writer requires at least one frame in flight"
            );
        }
        return frame_count;
    }

    auto checked_light_capacity(uint32_t max_light_count) -> uint32_t {
        if (max_light_count == 0) {
            throw std::invalid_argument(
                "light writer requires a non-zero light capacity"
            );
        }
        return max_light_count;
    }

    auto align_up(
        vk::DeviceSize value,
        vk::DeviceSize alignment
    ) -> vk::DeviceSize {
        if (alignment <= 1) {
            return value;
        }
        if (value > std::numeric_limits<vk::DeviceSize>::max() - alignment + 1) {
            throw std::overflow_error("light buffer alignment overflow");
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    auto light_data_size(uint32_t max_light_count) -> vk::DeviceSize {
        return static_cast<vk::DeviceSize>(max_light_count) *
            sizeof(GpuPointLight);
    }

    auto calculate_frame_stride(
        const Device& device,
        uint32_t max_light_count
    ) -> vk::DeviceSize {
        const auto data_size = light_data_size(max_light_count);
        const auto limits = device.physical_device().getProperties().limits;
        if (data_size > limits.maxStorageBufferRange) {
            throw std::invalid_argument(
                "light buffer frame data exceeds maxStorageBufferRange"
            );
        }
        return align_up(
            data_size,
            limits.minStorageBufferOffsetAlignment
        );
    }

    auto calculate_buffer_size(
        vk::DeviceSize frame_stride,
        uint32_t frame_count
    ) -> vk::DeviceSize {
        if (frame_stride >
            std::numeric_limits<vk::DeviceSize>::max() / frame_count) {
            throw std::overflow_error("light buffer size overflow");
        }
        return frame_stride * frame_count;
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eStorageBuffer,
                frame_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto allocate_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& descriptor_pool,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout,
        uint32_t frame_count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(
            frame_count,
            *descriptor_set_layout
        );
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*descriptor_pool)
            .setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(allocate_info);
    }
}

LightWriter::LightWriter(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::DescriptorSetLayout& descriptor_set_layout,
    uint32_t frame_count,
    uint32_t max_light_count
) : max_light_count_(checked_light_capacity(max_light_count)),
    frame_stride_(calculate_frame_stride(device, max_light_count_)),
    light_buffer_(
        allocator,
        BufferDesc{
            .size = calculate_buffer_size(
                frame_stride_,
                checked_frame_count(frame_count)
            ),
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }
    ),
    descriptor_pool_(
        create_descriptor_pool(device, checked_frame_count(frame_count))
    ),
    descriptor_sets_(
        allocate_descriptor_sets(
            device,
            descriptor_pool_,
            descriptor_set_layout,
            checked_frame_count(frame_count)
        )
    ),
    light_counts_(checked_frame_count(frame_count), 0),
    staging_data_(max_light_count_ * 3, glm::vec4{0.0F}) {
    const auto frame_data_size = light_data_size(max_light_count_);
    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::WriteDescriptorSet> descriptor_writes;
    buffer_infos.reserve(frame_count);
    descriptor_writes.reserve(frame_count);

    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const auto frame_offset = frame_stride_ * frame_index;
        light_buffer_.write(
            staging_data_.data(),
            frame_data_size,
            frame_offset
        );

        buffer_infos.push_back(
            vk::DescriptorBufferInfo{}
                .setBuffer(light_buffer_.get())
                .setOffset(frame_offset)
                .setRange(frame_data_size)
        );
        descriptor_writes.push_back(
            vk::WriteDescriptorSet{}
                .setDstSet(*descriptor_sets_[frame_index])
                .setDstBinding(0)
                .setDstArrayElement(0)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(buffer_infos.back())
        );
    }

    device.logical_device().updateDescriptorSets(descriptor_writes, {});
}

auto LightWriter::write(
    uint32_t frame_index,
    const std::vector<PointLight>& lights,
    std::span<const PointLightShadowBinding> shadow_bindings
) -> void {
    auto& light_count = light_counts_.at(frame_index);
    if (lights.size() > max_light_count_) {
        throw std::length_error(
            "point light count exceeds light writer capacity"
        );
    }
    if (lights.size() != shadow_bindings.size()) {
        throw std::invalid_argument(
            "point light and shadow binding counts do not match"
        );
    }

    std::ranges::fill(staging_data_, glm::vec4{0.0F});
    for (std::size_t index = 0; index < lights.size(); ++index) {
        const auto& light = lights[index];
        const auto& shadow = shadow_bindings[index];
        staging_data_[index * 3] = glm::vec4{
            light.position,
            light.intensity
        };
        staging_data_[index * 3 + 1] = glm::vec4{light.color, 0.0F};
        staging_data_[index * 3 + 2] = glm::vec4{
            static_cast<float>(shadow.shadow_index),
            shadow.far_plane,
            shadow.source_radius,
            shadow.near_plane
        };
    }

    light_buffer_.write(
        staging_data_.data(),
        light_data_size(max_light_count_),
        frame_stride_ * frame_index
    );
    light_count = static_cast<uint32_t>(lights.size());
}
