#ifndef CAMERA_UNIFORMS_HPP
#define CAMERA_UNIFORMS_HPP

#include <vector>

#include "gfx/descriptor/descriptor_pool.hpp"
#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/resource/buffer.hpp"
#include "utils/data/view_projection.hpp"

class CameraUniforms {
public:
    CameraUniforms(
        const Device& device,
        const GpuAllocator& allocator,
        const DescriptorSetLayout& descriptor_set_layout,
        const FrameContext& frame_context
    );

    CameraUniforms(const CameraUniforms&) = delete;
    auto operator=(const CameraUniforms&) -> CameraUniforms& = delete;
    CameraUniforms(CameraUniforms&&) = delete;
    auto operator=(CameraUniforms&&) -> CameraUniforms& = delete;

    // update the uniforms belonging to the current frame in flight
    auto update(const ViewProjection& uniforms) -> void;

    // return the uniform buffer belonging to the current frame in flight
    auto current_uniform_buffer() const -> const Buffer& {
        return uniform_buffers_.at(frame_context_.current_frame_index());
    }

    // return the descriptor set belonging to the current frame in flight
    auto current_descriptor_set() const -> const vk::raii::DescriptorSet& {
        return descriptor_sets_.at(frame_context_.current_frame_index());
    }

private:
    const FrameContext& frame_context_;
    DescriptorPool descriptor_pool_;
    std::vector<Buffer> uniform_buffers_;
    std::vector<vk::raii::DescriptorSet> descriptor_sets_;
};

#endif
