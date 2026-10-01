#include "render/pass/direct_light_composite/direct_light_composite_pass.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/direct_light_denoise/direct_light_denoise_pass.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/pass/restir_di/restir_di_pass.hpp"
#include "render/render_graph.hpp"

namespace {
    constexpr uint32_t sampled_image_count = 6;

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto create_descriptor_set_layout(const Device& device) -> vk::raii::DescriptorSetLayout {
        constexpr auto stage = vk::ShaderStageFlagBits::eFragment;
        std::array<vk::DescriptorSetLayoutBinding, sampled_image_count + 1> bindings{};
        bindings[0] = {0, vk::DescriptorType::eUniformBuffer, 1, stage};
        for (uint32_t index = 1; index < bindings.size(); ++index) {
            bindings[index] = {index, vk::DescriptorType::eSampledImage, 1, stage};
        }
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_pipeline_layout(const Device& device, const vk::raii::DescriptorSetLayout& descriptor_set_layout) -> vk::raii::PipelineLayout {
        const std::array layouts{*descriptor_set_layout};
        vk::PipelineLayoutCreateInfo create_info{};
        create_info.setSetLayouts(layouts);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(const Device& device, const std::filesystem::path& path) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        const vk::ShaderModuleCreateInfo create_info{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()};
        return device.logical_device().createShaderModule(create_info);
    }

    auto create_pipeline(const Device& device, const RenderGraph& render_graph, const vk::raii::PipelineLayout& layout) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(device, "./spv/direct_light_vertex.spv");
        const auto fragment_shader = create_shader_module(device, "./spv/direct_light_composite_fragment.spv");

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &layout;
        desc.color_attachment_formats = {render_graph.image(DirectLightCompositePass::output_resource).format()};
        desc.cull_mode = vk::CullModeFlagBits::eNone;
        desc.depth_test_enable = false;
        desc.depth_write_enable = false;
        desc.blend_enable = false;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto create_descriptor_pool(const Device& device, uint32_t frame_count) -> vk::raii::DescriptorPool {
        if (frame_count > std::numeric_limits<uint32_t>::max() / sampled_image_count) {
            throw std::overflow_error("direct-light composite descriptor count exceeds uint32_t");
        }
        const std::array sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, frame_count * sampled_image_count}
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet).setMaxSets(frame_count).setPoolSizes(sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto allocate_descriptor_sets(const Device& device, const vk::raii::DescriptorPool& pool,
                                  const vk::raii::DescriptorSetLayout& layout, uint32_t count) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(count, *layout);
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info.setDescriptorPool(*pool).setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(allocate_info);
    }

    auto create_command_slots(const Device& device, uint32_t frame_count) -> std::vector<CommandSlot> {
        std::vector<CommandSlot> slots;
        slots.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            auto pool = device.logical_device().createCommandPool(vk::CommandPoolCreateInfo{
                .flags = vk::CommandPoolCreateFlagBits::eTransient,
                .queueFamilyIndex = device.graphics_family()
            });
            const vk::CommandBufferAllocateInfo allocate_info{
                .commandPool = *pool,
                .level = vk::CommandBufferLevel::eSecondary,
                .commandBufferCount = 1
            };
            auto command_buffers = device.logical_device().allocateCommandBuffers(allocate_info);
            slots.push_back(CommandSlot{.pool = std::move(pool), .command_buffer = std::move(command_buffers.front())});
        }
        return slots;
    }

    auto descriptor_image_info(const Image& image) -> vk::DescriptorImageInfo {
        return {.imageView = *image.view(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    }

    auto descriptor_buffer_info(const Buffer& buffer) -> vk::DescriptorBufferInfo {
        return {.buffer = buffer.get(), .offset = 0, .range = buffer.size()};
    }
}

struct DirectLightCompositePass::Impl {
    explicit Impl(const RestirDiPass& restir) : restir_pass(restir) {}

    const RestirDiPass& restir_pass;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;
    std::vector<CommandSlot> command_slots;
    bool initialized = false;
};

DirectLightCompositePass::DirectLightCompositePass(const Device& device, RenderGraph& render_graph, const RestirDiPass& restir_di_pass)
    : RenderPass(std::string{pass_name}, device, render_graph), impl_(std::make_unique<Impl>(restir_di_pass)) {}

DirectLightCompositePass::~DirectLightCompositePass() = default;

auto DirectLightCompositePass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("direct-light composite pass is already initialized");
    }

    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    impl_->pipeline_layout = create_pipeline_layout(device_, impl_->descriptor_set_layout);
    impl_->pipeline = create_pipeline(device_, render_graph_, impl_->pipeline_layout);

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->descriptor_pool = create_descriptor_pool(device_, frame_count);
    impl_->descriptor_sets = allocate_descriptor_sets(device_, impl_->descriptor_pool, impl_->descriptor_set_layout, frame_count);

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        const auto buffer_info = descriptor_buffer_info(impl_->restir_pass.frame_buffer(frame));
        const std::array image_infos{
            descriptor_image_info(render_graph_.image(DirectLightDenoisePass::diffuse_resource, frame)),
            descriptor_image_info(render_graph_.image(DirectLightDenoisePass::specular_resource, frame)),
            descriptor_image_info(render_graph_.image(GeometryPass::base_color_ao_resource, frame)),
            descriptor_image_info(render_graph_.image(GeometryPass::normal_roughness_resource, frame)),
            descriptor_image_info(render_graph_.image(GeometryPass::depth_resource, frame)),
            descriptor_image_info(render_graph_.image(GeometryPass::emissive_metallic_resource, frame))
        };

        std::array<vk::WriteDescriptorSet, sampled_image_count + 1> writes{};
        writes[0].setDstSet(*impl_->descriptor_sets[frame]).setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer).setBufferInfo(buffer_info);
        for (uint32_t index = 0; index < image_infos.size(); ++index) {
            writes[index + 1].setDstSet(*impl_->descriptor_sets[frame]).setDstBinding(index + 1)
                .setDescriptorType(vk::DescriptorType::eSampledImage).setImageInfo(image_infos[index]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->command_slots = create_command_slots(device_, frame_count);
    impl_->initialized = true;
}

auto DirectLightCompositePass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), DirectLightDenoisePass::pass_name);
    render_graph.set_image_usage(name(), DirectLightDenoisePass::diffuse_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), DirectLightDenoisePass::specular_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::base_color_ao_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::normal_roughness_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::depth_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::emissive_metallic_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), output_resource, ImageUsage::ColorAttachment);
}

auto DirectLightCompositePass::prepare(const Scene&) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("direct-light composite pass must be initialized before preparation");
    }
    const auto frame = render_graph_.current_frame_index();
    const auto buffer_info = descriptor_buffer_info(impl_->restir_pass.frame_buffer(frame));
    std::array<vk::WriteDescriptorSet, 1> writes{};
    writes[0].setDstSet(*impl_->descriptor_sets.at(frame)).setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer).setBufferInfo(buffer_info);
    device_.logical_device().updateDescriptorSets(writes, {});
}

auto DirectLightCompositePass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("direct-light composite pass must be initialized before recording");
    }

    const auto frame = render_graph_.current_frame_index();
    auto& slot = impl_->command_slots.at(frame);
    slot.pool.reset();

    const std::array color_formats{render_graph_.image(output_resource).format()};
    vk::CommandBufferInheritanceRenderingInfo rendering_inheritance{};
    rendering_inheritance.setColorAttachmentFormats(color_formats).setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::CommandBufferInheritanceInfo inheritance{};
    inheritance.setPNext(&rendering_inheritance);
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit | vk::CommandBufferUsageFlagBits::eRenderPassContinue)
        .setPInheritanceInfo(&inheritance);

    auto& command_buffer = slot.command_buffer;
    command_buffer.begin(begin_info);
#ifndef NDEBUG
    insert_debug_marker(command_buffer, name().data());
#endif
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *impl_->pipeline);

    const auto extent_3d = render_graph_.image(output_resource).extent();
    const vk::Extent2D extent{extent_3d.width, extent_3d.height};
    command_buffer.setViewport(0, vk::Viewport{
        .x = 0.0F, .y = 0.0F,
        .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height),
        .minDepth = 0.0F, .maxDepth = 1.0F
    });
    command_buffer.setScissor(0, vk::Rect2D{.offset = vk::Offset2D{0, 0}, .extent = extent});

    const std::array descriptor_sets{*impl_->descriptor_sets.at(frame)};
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *impl_->pipeline_layout, 0, descriptor_sets, {});
    command_buffer.draw(3, 1, 0, 0);
    command_buffer.end();
    return *command_buffer;
}
