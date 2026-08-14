#include "render/pass/forward/camera_writer.hpp"

#include <array>
#include <stdexcept>
#include <vector>

namespace {
    auto checked_frame_count(uint32_t frame_count) -> uint32_t {
        if (frame_count == 0) {
            throw std::invalid_argument(
                "camera writer requires at least one frame in flight"
            );
        }
        return frame_count;
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        vk::DescriptorPoolSize uniform_buffer_pool_size{};
        uniform_buffer_pool_size
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(frame_count);

        const std::array pool_sizes{uniform_buffer_pool_size};
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_uniform_buffers(
        const MemoryAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t frame_index = 0;
             frame_index < frame_count;
             ++frame_index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(ViewProjection),
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                    .memory = BufferMemoryUsage::Upload,
                    .persistent_mapping = true
                }
            );
        }
        return buffers;
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

CameraWriter::CameraWriter(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::DescriptorSetLayout& descriptor_set_layout,
    uint32_t frame_count
) : descriptor_pool_(
        create_descriptor_pool(device, checked_frame_count(frame_count))
    ),
    uniform_buffers_(
        create_uniform_buffers(allocator, checked_frame_count(frame_count))
    ),
    descriptor_sets_(
        allocate_descriptor_sets(
            device,
            descriptor_pool_,
            descriptor_set_layout,
            checked_frame_count(frame_count)
        )
    ) {
    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::WriteDescriptorSet> descriptor_writes;
    buffer_infos.reserve(frame_count);
    descriptor_writes.reserve(frame_count);

    const ViewProjection initial_uniforms{};
    for (uint32_t frame_index = 0;
         frame_index < frame_count;
         ++frame_index) {
        auto& uniform_buffer = uniform_buffers_[frame_index];
        uniform_buffer.write(&initial_uniforms, sizeof(initial_uniforms));

        buffer_infos.push_back(
            vk::DescriptorBufferInfo{}
                .setBuffer(uniform_buffer.get())
                .setOffset(0)
                .setRange(sizeof(ViewProjection))
        );

        descriptor_writes.push_back(
            vk::WriteDescriptorSet{}
                .setDstSet(*descriptor_sets_[frame_index])
                .setDstBinding(0)
                .setDstArrayElement(0)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setBufferInfo(buffer_infos.back())
        );
    }

    device.logical_device().updateDescriptorSets(descriptor_writes, {});
}

auto CameraWriter::write(
    uint32_t frame_index,
    const ViewProjection& uniforms
) -> void {
    uniform_buffers_.at(frame_index).write(&uniforms, sizeof(uniforms));
}
