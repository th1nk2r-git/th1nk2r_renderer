#include "render/pass/forward/forward_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include "gfx/pipeline/graphics_pipeline.hpp"
#include "scene/scene.hpp"
#include "io/spirv_loader.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/registry/resource_registry.hpp"

namespace {
    struct alignas(16) DrawConstants {
        glm::mat4 transform{1.0F};
        glm::vec4 normal_column_0{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_1{0.0F, 1.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_2{0.0F, 0.0F, 1.0F, 0.0F};
        uint32_t point_light_count = 0;
        uint32_t padding_0 = 0;
        uint32_t padding_1 = 0;
        uint32_t padding_2 = 0;
    };

    static_assert(sizeof(DrawConstants) == 128);

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
            .setStageFlags(
                vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment
            );

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

        vk::DescriptorSetLayoutBinding metallic_roughness_texture_binding{};
        metallic_roughness_texture_binding
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding normal_texture_binding{};
        normal_texture_binding
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding occlusion_texture_binding{};
        occlusion_texture_binding
            .setBinding(4)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding emissive_texture_binding{};
        emissive_texture_binding
            .setBinding(5)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding material_parameter_binding{};
        material_parameter_binding
            .setBinding(6)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding light_storage_binding{};
        light_storage_binding
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding ibl_sampler_binding{};
        ibl_sampler_binding
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding irradiance_binding{};
        irradiance_binding
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding prefiltered_binding{};
        prefiltered_binding
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding brdf_lut_binding{};
        brdf_lut_binding
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        vk::DescriptorSetLayoutBinding environment_binding{};
        environment_binding
            .setBinding(4)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        const std::array camera_bindings{camera_uniform_binding};
        const std::array material_bindings{
            sampler_binding,
            base_color_texture_binding,
            metallic_roughness_texture_binding,
            normal_texture_binding,
            occlusion_texture_binding,
            emissive_texture_binding,
            material_parameter_binding
        };
        const std::array light_bindings{light_storage_binding};
        const std::array ibl_bindings{
            ibl_sampler_binding,
            irradiance_binding,
            prefiltered_binding,
            brdf_lut_binding,
            environment_binding
        };
        std::vector<vk::raii::DescriptorSetLayout> layouts;
        layouts.reserve(4);
        layouts.emplace_back(
            create_descriptor_set_layout(device, camera_bindings)
        );
        layouts.emplace_back(
            create_descriptor_set_layout(device, material_bindings)
        );
        layouts.emplace_back(
            create_descriptor_set_layout(device, light_bindings)
        );
        layouts.emplace_back(
            create_descriptor_set_layout(device, ibl_bindings)
        );
        return layouts;
    }

    auto create_pipeline_layout(
        const Device& device,
        const std::vector<vk::raii::DescriptorSetLayout>& descriptor_set_layouts,
        const vk::raii::DescriptorSetLayout& shadow_descriptor_set_layout
    ) -> vk::raii::PipelineLayout {
        std::vector<vk::DescriptorSetLayout> layout_handles;
        layout_handles.reserve(descriptor_set_layouts.size() + 1);
        for (const auto& layout : descriptor_set_layouts) {
            layout_handles.push_back(*layout);
        }
        layout_handles.push_back(*shadow_descriptor_set_layout);

        const std::array push_constant_ranges{
            vk::PushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(DrawConstants)
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
            },
            vk::VertexInputAttributeDescription{
                .location = 3,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, normal)
            },
            vk::VertexInputAttributeDescription{
                .location = 4,
                .binding = 0,
                .format = vk::Format::eR32G32B32A32Sfloat,
                .offset = offsetof(Vertex, tangent)
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

    auto create_skybox_pipeline(
        const Device& device,
        const vk::raii::RenderPass& render_pass,
        const vk::raii::PipelineLayout& pipeline_layout
    ) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(
            device,
            "./spv/skybox_vertex.spv"
        );
        const auto fragment_shader = create_shader_module(
            device,
            "./spv/skybox_fragment.spv"
        );

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &pipeline_layout;
        desc.render_pass = &render_pass;
        desc.cull_mode = vk::CullModeFlagBits::eNone;
        desc.depth_test_enable = true;
        desc.depth_write_enable = false;
        desc.depth_compare_op = vk::CompareOp::eLessOrEqual;
        return GraphicsPipelineFactory::create(device, desc);
    }
}

ForwardPass::ForwardPass(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::RenderPass& render_pass,
    const vk::raii::DescriptorSetLayout& shadow_descriptor_set_layout,
    uint32_t frame_count
) : descriptor_set_layouts_(create_descriptor_set_layouts(device)),
    pipeline_layout_(
        create_pipeline_layout(
            device,
            descriptor_set_layouts_,
            shadow_descriptor_set_layout
        )
    ),
    pipeline_(create_pipeline(device, render_pass, pipeline_layout_)),
    skybox_pipeline_(
        create_skybox_pipeline(device, render_pass, pipeline_layout_)
    ),
    camera_writer_(
        device,
        allocator,
        descriptor_set_layouts_.at(0),
        frame_count
    ),
    material_writer_(
        device,
        allocator,
        descriptor_set_layouts_.at(1)
    ),
    light_writer_(
        device,
        allocator,
        descriptor_set_layouts_.at(2),
        frame_count
    ),
    ibl_writer_(
        device,
        allocator,
        descriptor_set_layouts_.at(3)
    ) {}

auto ForwardPass::write_material(
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

auto ForwardPass::write_environment(
    const HdrImageData& panorama,
    ImageUploader& uploader
) -> void {
    ibl_writer_.write(panorama, uploader);
}

auto ForwardPass::record(
    ExecutionContext context,
    Input input,
    Output output,
    const OverlayRecorder& overlay_recorder
) -> void {
    const auto aspect_ratio = static_cast<float>(output.extent.width) / static_cast<float>(output.extent.height);
    camera_writer_.write(
        context.frame_index,
        ViewProjection{
            .view = input.scene.camera().view_matrix(),
            .projection = input.scene.camera().projection_matrix(aspect_ratio),
            .camera_position = glm::vec4{
                input.scene.camera().position(),
                1.0F
            }
        }
    );
    light_writer_.write(
        context.frame_index,
        input.scene.point_lights(),
        input.shadow.light_bindings
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
        .setRenderPass(*output.render_pass)
        .setFramebuffer(*output.framebuffer)
        .setRenderArea(vk::Rect2D{{0, 0}, output.extent})
        .setClearValues(clear_values);

    auto& command_buffer = context.command_buffer;
    command_buffer.beginRenderPass(
        render_pass_info,
        vk::SubpassContents::eInline
    );

    const vk::Viewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(output.extent.width),
        .height = static_cast<float>(output.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };
    const vk::Rect2D scissor{{0, 0}, output.extent};
    command_buffer.setViewport(0, viewport);
    command_buffer.setScissor(0, scissor);
    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_
    );

    const std::array camera_descriptor_sets{
        *camera_writer_.descriptor_set(context.frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        0,
        camera_descriptor_sets,
        {}
    );

    const std::array shadow_descriptor_sets{
        input.shadow.descriptor_set
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        4,
        shadow_descriptor_sets,
        {}
    );

    const std::array light_descriptor_sets{
        *light_writer_.descriptor_set(context.frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        2,
        light_descriptor_sets,
        {}
    );

    const std::array ibl_descriptor_sets{
        *ibl_writer_.descriptor_set()
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        3,
        ibl_descriptor_sets,
        {}
    );

    for (const auto& entity : input.scene.entities()) {
        const auto model_matrix = entity.model_matrix();
        const auto normal_matrix = glm::transpose(
            glm::inverse(glm::mat3{model_matrix})
        );
        command_buffer.pushConstants<DrawConstants>(
            *pipeline_layout_,
            vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment,
            0,
            DrawConstants{
                .transform = model_matrix,
                .normal_column_0 = glm::vec4{normal_matrix[0], 0.0F},
                .normal_column_1 = glm::vec4{normal_matrix[1], 0.0F},
                .normal_column_2 = glm::vec4{normal_matrix[2], 0.0F},
                .point_light_count = light_writer_.light_count(
                    context.frame_index
                )
            }
        );

        const auto& model = input.registry.query(entity.model_id());
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

    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *skybox_pipeline_
    );
    command_buffer.draw(36, 1, 0, 0);

    if (overlay_recorder) {
        overlay_recorder(command_buffer);
    }

    command_buffer.endRenderPass();
}

auto ForwardPass::recreate_pipeline(const Device& device, const vk::raii::RenderPass& render_pass) -> void {
    auto replacement = create_pipeline(device, render_pass, pipeline_layout_);
    auto skybox_replacement = create_skybox_pipeline(
        device,
        render_pass,
        pipeline_layout_
    );
    pipeline_ = std::move(replacement);
    skybox_pipeline_ = std::move(skybox_replacement);
}
