#ifndef GRAPHICS_PIPELINE_DESC_HPP
#define GRAPHICS_PIPELINE_DESC_HPP

#include <cstdint>
#include <vector>

#include "gfx/pipeline/pipeline_layout.hpp"
#include "gfx/pipeline/shader_module.hpp"
#include "gfx/swapchain/render_pass.hpp"

struct GraphicsPipelineDesc {
    const ShaderModule* vertex_shader = nullptr;
    const ShaderModule* fragment_shader = nullptr;

    const PipelineLayout* layout = nullptr;
    const RenderPass* render_pass = nullptr;
    uint32_t subpass = 0;

    std::vector<vk::VertexInputBindingDescription> vertex_bindings;

    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;

    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

    vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;

    vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;

    bool blend_enable = false;
};

#endif
