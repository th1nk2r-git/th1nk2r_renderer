#ifndef CAMERA_CONTEXT_HPP
#define CAMERA_CONTEXT_HPP

#include <vector>

#include "gfx/descriptor/descriptor_pool.hpp"
#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/resources/buffer.hpp"
#include <glm/mat4x4.hpp>

struct alignas(16) CameraUniforms {
    glm::mat4 view {1.0F};
    glm::mat4 projection {1.0F};
};

class CameraUniformsContext {
public:
    CameraUniformsContext(
        const Device& device,
        const GpuAllocator& allocator,
        DescriptorPool& descriptor_pool,
        const DescriptorSetLayout& descriptor_set_layout,
        const FrameContext& frame_context
    );

    CameraUniformsContext(const CameraUniformsContext&) = delete;
    auto operator=(const CameraUniformsContext&) -> CameraUniformsContext& = delete;

    CameraUniformsContext(CameraUniformsContext&&) = delete;
    auto operator=(CameraUniformsContext&&) -> CameraUniformsContext& = delete;

    // update the uniforms belonging to the current frame in flight
    auto update(const CameraUniforms& uniforms) -> void;

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
    std::vector<Buffer> uniform_buffers_;
    std::vector<vk::raii::DescriptorSet> descriptor_sets_;
};

#endif
