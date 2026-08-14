#include "render/pass/forward/forward_pass.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

#include <glm/mat4x4.hpp>

#include "gfx/pipeline/graphics_pipeline.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include "io/spirv_loader.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/registry/resource_registry.hpp"

namespace {
    auto create_descriptor_set_layout(const Device& device, std::span<const vk::DescriptorSetLayoutBinding> bindings) -> vk::raii::DescriptorSetLayout {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_descriptor_set_layouts(const Device& device) -> std::vector<vk::raii::DescriptorSetLayout> {
        vk::DescriptorSetLayoutBinding camera_uniform_binding{};
        camera_uniform_binding
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex);

        vk::DescriptorSetLayoutBinding sampler_binding{};
        sampler_binding
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding base_color_texture_binding{};
        base_color_texture_binding
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        const std::array camera_bindings{camera_uniform_binding};
        const std::array material_bindings{
            sampler_binding,
            base_color_texture_binding
        };
        std::vector<vk::raii::DescriptorSetLayout> layouts;
        layouts.reserve(2);
        layouts.emplace_back(
            create_descriptor_set_layout(device, camera_bindings)
        );
        layouts.emplace_back(
            create_descriptor_set_layout(device, material_bindings)
        );
        return layouts;
    }

    auto create_pipeline_layout(const Device& device, const std::vector<vk::raii::DescriptorSetLayout>& descriptor_set_layouts) -> vk::raii::PipelineLayout {
        std::vector<vk::DescriptorSetLayout> layout_handles;
        layout_handles.reserve(descriptor_set_layouts.size());
        for (const auto& layout : descriptor_set_layouts) {
            layout_handles.push_back(*layout);
        }

        const std::array push_constant_ranges{
            vk::PushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eVertex,
                .offset = 0,
                .size = sizeof(glm::mat4)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(layout_handles)
            .setPushConstantRanges(push_constant_ranges);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(const Device& device, const std::filesystem::path& path) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        const vk::ShaderModuleCreateInfo create_info{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()
        };
        return device.logical_device().createShaderModule(create_info);
    }

    auto create_pipeline(const Device& device, const vk::raii::RenderPass& render_pass, const vk::raii::PipelineLayout& pipeline_layout) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(device, "./spv/vertex.spv");
        const auto fragment_shader = create_shader_module(device, "./spv/fragment.spv");

        const vk::VertexInputBindingDescription vertex_binding{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex
        };
        const std::vector vertex_attributes{
            vk::VertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, position)
            },
            vk::VertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color)
            },
            vk::VertexInputAttributeDescription{
                .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, texcoord)
            }
        };

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &pipeline_layout;
        desc.render_pass = &render_pass;
        desc.vertex_bindings = {vertex_binding};
        desc.vertex_attributes = vertex_attributes;
        desc.depth_test_enable = true;
        desc.depth_write_enable = true;
        return GraphicsPipelineFactory::create(device, desc);
    }
}

ForwardPass::ForwardPass(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::RenderPass& render_pass,
    uint32_t frame_count
) : descriptor_set_layouts_(create_descriptor_set_layouts(device)),
    pipeline_layout_(create_pipeline_layout(device, descriptor_set_layouts_)),
    pipeline_(create_pipeline(device, render_pass, pipeline_layout_)),
    camera_writer_(
        device,
        allocator,
        descriptor_set_layouts_.at(0),
        frame_count
    ),
    material_writer_(device, descriptor_set_layouts_.at(1)) {}

auto ForwardPass::write_material_descriptors(
    std::span<const ResourceId<Material>> material_ids,
    const ResourceRegistry& registry
) -> void {
    for (const auto material_id : material_ids) {
        material_writer_.write(
            material_id,
            registry.query(material_id)
        );
    }
}

auto ForwardPass::record(
    const RenderFrameContext& frame,
    const Scene& scene,
    const ResourceRegistry& registry
) -> void {
    const auto aspect_ratio = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
    camera_writer_.write(
        frame.frame_index,
        ViewProjection{
            .view = scene.camera().view_matrix(),
            .projection = scene.camera().projection_matrix(aspect_ratio)
        }
    );

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color.float32[0] = 0.02F;
    clear_values[0].color.float32[1] = 0.02F;
    clear_values[0].color.float32[2] = 0.03F;
    clear_values[0].color.float32[3] = 1.0F;
    clear_values[1].depthStencil.depth = 1.0F;
    clear_values[1].depthStencil.stencil = 0;

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info
        .setRenderPass(*frame.render_pass)
        .setFramebuffer(*frame.framebuffer)
        .setRenderArea(vk::Rect2D{{0, 0}, frame.extent})
        .setClearValues(clear_values);

    auto& command_buffer = frame.command_buffer;
    command_buffer.beginRenderPass(
        render_pass_info,
        vk::SubpassContents::eInline
    );

    const vk::Viewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(frame.extent.width),
        .height = static_cast<float>(frame.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };
    const vk::Rect2D scissor{{0, 0}, frame.extent};
    command_buffer.setViewport(0, viewport);
    command_buffer.setScissor(0, scissor);
    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_
    );

    const std::array camera_descriptor_sets{
        *camera_writer_.descriptor_set(frame.frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        0,
        camera_descriptor_sets,
        {}
    );

    for (const auto& entity : scene.entities()) {
        command_buffer.pushConstants<glm::mat4>(
            *pipeline_layout_,
            vk::ShaderStageFlagBits::eVertex,
            0,
            entity.model_matrix()
        );

        const auto& model = registry.query(entity.model_id());
        for (const auto& mesh : model.meshes()) {
            const std::array material_descriptor_sets{
                *material_writer_.descriptor_set(mesh.material())
            };
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pipeline_layout_,
                1,
                material_descriptor_sets,
                {}
            );

            mesh.bind(command_buffer);
            command_buffer.drawIndexed(mesh.index_count(), 1, 0, 0, 0);
        }
    }

    command_buffer.endRenderPass();
}

auto ForwardPass::recreate_pipeline(const Device& device, const vk::raii::RenderPass& render_pass) -> void {
    auto replacement = create_pipeline(device, render_pass, pipeline_layout_);
    pipeline_ = std::move(replacement);
}
