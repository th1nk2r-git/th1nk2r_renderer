#include "render/uniforms_context.hpp"

#include <stdexcept>
#include <vector>

namespace {
    auto checked_frame_count(const FrameContext& frame_context) -> uint32_t {
        const auto frame_count = frame_context.frame_count();
        if (frame_count == 0) {
            throw std::invalid_argument(
                "camera context requires at least one frame in flight"
            );
        }
        return frame_count;
    }

    auto create_uniform_buffers(
        const GpuAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(UniformBufferObject),
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
        DescriptorPool& descriptor_pool,
        const DescriptorSetLayout& descriptor_set_layout,
        uint32_t frame_count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(
            frame_count,
            *descriptor_set_layout.get()
        );
        return descriptor_pool.allocate_sets(device, layouts);
    }
}

UniformsContext::UniformsContext(
    const Device& device,
    const GpuAllocator& allocator,
    DescriptorPool& descriptor_pool,
    const DescriptorSetLayout& descriptor_set_layout,
    const FrameContext& frame_context
)
    : frame_context_(frame_context),
      uniform_buffers_(create_uniform_buffers(
          allocator,
          checked_frame_count(frame_context_)
      )),
      descriptor_sets_(allocate_descriptor_sets(
          device,
          descriptor_pool,
          descriptor_set_layout,
          static_cast<uint32_t>(uniform_buffers_.size())
      )) {
    const auto frame_count = frame_context_.frame_count();

    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::WriteDescriptorSet> descriptor_writes;
    buffer_infos.reserve(frame_count);
    descriptor_writes.reserve(frame_count);

    const UniformBufferObject initial_uniforms{};
    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        auto& uniform_buffer = uniform_buffers_[frame_index];
        uniform_buffer.write(&initial_uniforms, sizeof(initial_uniforms));

        vk::DescriptorBufferInfo buffer_info{};
        buffer_info
            .setBuffer(uniform_buffer.get())
            .setOffset(0)
            .setRange(sizeof(UniformBufferObject));
        buffer_infos.push_back(buffer_info);

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write
            .setDstSet(*descriptor_sets_[frame_index])
            .setDstBinding(0)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(buffer_infos.back());
        descriptor_writes.push_back(descriptor_write);
    }

    device.logical_device().updateDescriptorSets(descriptor_writes, {});
}

auto UniformsContext::update(const UniformBufferObject& uniforms) -> void {
    const auto frame_index = frame_context_.current_frame_index();
    uniform_buffers_.at(frame_index).write(&uniforms, sizeof(uniforms));
}
