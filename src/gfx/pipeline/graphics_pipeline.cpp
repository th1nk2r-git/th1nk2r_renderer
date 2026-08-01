#include "gfx/pipeline/graphics_pipeline.hpp"

#include <array>
#include <stdexcept>
#include <utility>

auto GraphicsPipelineFactory::create(const Device& device, const GraphicsPipelineDesc& desc) -> Pipeline {
    if (desc.vertex_shader == nullptr) {
        throw std::invalid_argument("graphics pipeline requires a vertex shader!");
    }
    if (desc.fragment_shader == nullptr) {
        throw std::invalid_argument("graphics pipeline requires a fragment shader!");
    }
    if (desc.layout == nullptr) {
        throw std::invalid_argument("graphics pipeline requires a pipeline layout!");
    }
    if (desc.render_pass == nullptr) {
        throw std::invalid_argument("graphics pipeline requires a render pass!");
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages{};
    shader_stages[0]
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(desc.vertex_shader->get())
        .setPName("main");
    shader_stages[1]
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(desc.fragment_shader->get())
        .setPName("main");

    vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state
        .setVertexBindingDescriptions(desc.vertex_bindings)
        .setVertexAttributeDescriptions(desc.vertex_attributes);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state
        .setTopology(desc.topology)
        .setPrimitiveRestartEnable(false);

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state
        .setViewportCount(1)
        .setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state
        .setDepthClampEnable(false)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(desc.cull_mode)
        .setFrontFace(desc.front_face)
        .setDepthBiasEnable(false)
        .setLineWidth(1.0F);

    vk::PipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state
        .setRasterizationSamples(vk::SampleCountFlagBits::e1)
        .setSampleShadingEnable(false);

    vk::PipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment
        .setBlendEnable(desc.blend_enable)
        .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd)
        .setColorWriteMask(
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA
        );

    vk::PipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state
        .setLogicOpEnable(false)
        .setAttachments(color_blend_attachment);

    constexpr std::array dynamic_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.setDynamicStates(dynamic_states);

    vk::GraphicsPipelineCreateInfo create_info{};
    create_info
        .setStages(shader_stages)
        .setPVertexInputState(&vertex_input_state)
        .setPInputAssemblyState(&input_assembly_state)
        .setPViewportState(&viewport_state)
        .setPRasterizationState(&rasterization_state)
        .setPMultisampleState(&multisample_state)
        .setPDepthStencilState(nullptr)
        .setPColorBlendState(&color_blend_state)
        .setPDynamicState(&dynamic_state)
        .setLayout(desc.layout->get())
        .setRenderPass(desc.render_pass->get())
        .setSubpass(desc.subpass);

    auto handle = device.logical_device().createGraphicsPipeline(
        nullptr,
        create_info
    );

    return Pipeline{
        std::move(handle),
        vk::PipelineBindPoint::eGraphics
    };
}
