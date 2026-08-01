#include "render/camera_uniforms_context.hpp"

#include <stdexcept>
#include <vector>

CameraUniformsContext::CameraUniformsContext(
    const Device& device,
    const GpuAllocator& allocator,
    DescriptorPool& descriptor_pool,
    const DescriptorSetLayout& descriptor_set_layout,
    const FrameContext& frame_context
)
    : frame_context_(frame_context) {
    const auto frame_count = frame_context_.frame_count();
    if (frame_count == 0) {
        throw std::invalid_argument(
            "camera context requires at least one frame in flight"
        );
    }

    uniform_buffers_.reserve(frame_count);
    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        uniform_buffers_.emplace_back(
            allocator,
            BufferDesc{
                .size = sizeof(CameraUniforms),
                .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                .memory = BufferMemoryUsage::Upload,
                .persistent_mapping = true
            }
        );
    }

    const std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(
        frame_count,
        *descriptor_set_layout.get()
    );
    descriptor_sets_ = descriptor_pool.allocate_sets(
        device,
        descriptor_set_layouts
    );

    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::WriteDescriptorSet> descriptor_writes;
    buffer_infos.reserve(frame_count);
    descriptor_writes.reserve(frame_count);

    const CameraUniforms initial_uniforms{};
    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        auto& uniform_buffer = uniform_buffers_[frame_index];
        uniform_buffer.write(&initial_uniforms, sizeof(initial_uniforms));

        vk::DescriptorBufferInfo buffer_info{};
        buffer_info
            .setBuffer(uniform_buffer.get())
            .setOffset(0)
            .setRange(sizeof(CameraUniforms));
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

auto CameraUniformsContext::update(const CameraUniforms& uniforms) -> void {
    const auto frame_index = frame_context_.current_frame_index();
    uniform_buffers_.at(frame_index).write(&uniforms, sizeof(uniforms));
}
