#include "render/pipeline/basic_rendering.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/pipeline/shader_module.hpp"
#include "geometry/vertex.hpp"

auto BasicRenderingPipelineFactory::create(const Device& device, const RenderPass& render_pass) -> PipelineState {
    vk::DescriptorSetLayoutBinding camera_uniform_binding{};
    camera_uniform_binding
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    const std::array descriptor_bindings{
        camera_uniform_binding
    };

    std::vector<DescriptorSetLayout> descriptor_set_layouts;
    descriptor_set_layouts.emplace_back(device, descriptor_bindings);

    const std::array set_layout_handles{
        *descriptor_set_layouts.front().get()
    };
    PipelineLayout pipeline_layout{device, set_layout_handles};

    const ShaderModule vertex_shader{device, "./spv/vertex.spv"};
    const ShaderModule fragment_shader{device, "./spv/fragment.spv"};

    vk::VertexInputBindingDescription vertex_binding{};
    vertex_binding
        .setBinding(0)
        .setStride(sizeof(Vertex))
        .setInputRate(vk::VertexInputRate::eVertex);

    vk::VertexInputAttributeDescription position_attribute{};
    position_attribute
        .setLocation(0)
        .setBinding(0)
        .setFormat(vk::Format::eR32G32Sfloat)
        .setOffset(offsetof(Vertex, position));

    vk::VertexInputAttributeDescription color_attribute{};
    color_attribute
        .setLocation(1)
        .setBinding(0)
        .setFormat(vk::Format::eR32G32B32Sfloat)
        .setOffset(offsetof(Vertex, color));

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.vertex_shader = &vertex_shader;
    pipeline_desc.fragment_shader = &fragment_shader;
    pipeline_desc.layout = &pipeline_layout;
    pipeline_desc.render_pass = &render_pass;
    pipeline_desc.vertex_bindings = {vertex_binding};
    pipeline_desc.vertex_attributes = {
        position_attribute,
        color_attribute
    };

    auto pipeline = GraphicsPipelineFactory::create(device, pipeline_desc);

    return PipelineState{
        std::move(descriptor_set_layouts),
        std::move(pipeline_layout),
        std::move(pipeline)
    };
}
