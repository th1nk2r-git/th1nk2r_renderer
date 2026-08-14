#ifndef GRAPHICS_PIPELINE_HPP
#define GRAPHICS_PIPELINE_HPP

#include <vector>

#include "gfx/device/device.hpp"

struct GraphicsPipelineDesc {
    const vk::raii::ShaderModule* vertex_shader = nullptr;
    const vk::raii::ShaderModule* fragment_shader = nullptr;

    const vk::raii::PipelineLayout* layout = nullptr;
    const vk::raii::RenderPass* render_pass = nullptr;
    uint32_t subpass = 0;

    std::vector<vk::VertexInputBindingDescription> vertex_bindings;

    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;

    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

    vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;

    vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;

    bool depth_test_enable = false;

    bool depth_write_enable = false;

    vk::CompareOp depth_compare_op = vk::CompareOp::eLess;

    bool blend_enable = false;
};

namespace GraphicsPipelineFactory {
    auto create(
        const Device& device,
        const GraphicsPipelineDesc& desc
    ) -> vk::raii::Pipeline;
}

#endif
