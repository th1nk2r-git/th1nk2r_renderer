#include "render/pass/light_composite/light_composite_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/denoise/denoise_pass.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/render_graph.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t sampled_image_count = 6;

    struct alignas(16) GpuCompositeFrame {
        glm::mat4 inverse_view_projection{1.0F};
        glm::vec4 camera_position{0.0F};
        glm::uvec4 viewport{0U};
    };

    static_assert(sizeof(GpuCompositeFrame) == 96);
    static_assert(offsetof(GpuCompositeFrame, camera_position) == 64);
    static_assert(offsetof(GpuCompositeFrame, viewport) == 80);

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
        const auto vertex_shader = create_shader_module(device, "./spv/light_composite_vertex.spv");
        const auto fragment_shader = create_shader_module(device, "./spv/light_composite_fragment.spv");

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &layout;
        desc.color_attachment_formats = {render_graph.image(LightCompositePass::output_resource).format()};
        desc.cull_mode = vk::CullModeFlagBits::eNone;
        desc.depth_test_enable = false;
        desc.depth_write_enable = false;
        desc.blend_enable = false;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto create_descriptor_pool(const Device& device, uint32_t frame_count) -> vk::raii::DescriptorPool {
        if (frame_count > std::numeric_limits<uint32_t>::max() / sampled_image_count) {
            throw std::overflow_error("light composite descriptor count exceeds uint32_t");
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

struct LightCompositePass::Impl {
    explicit Impl(const MemoryAllocator& memory_allocator) : allocator(memory_allocator) {}

    const MemoryAllocator& allocator;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;
    std::vector<Buffer> frame_buffers;
    std::vector<CommandSlot> command_slots;
    bool initialized = false;
};

LightCompositePass::LightCompositePass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph)
    : RenderPass(std::string{pass_name}, device, render_graph), impl_(std::make_unique<Impl>(allocator)) {}

LightCompositePass::~LightCompositePass() = default;

auto LightCompositePass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("light composite pass is already initialized");
    }

    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    impl_->pipeline_layout = create_pipeline_layout(device_, impl_->descriptor_set_layout);
    impl_->pipeline = create_pipeline(device_, render_graph_, impl_->pipeline_layout);

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->descriptor_pool = create_descriptor_pool(device_, frame_count);
    impl_->descriptor_sets = allocate_descriptor_sets(device_, impl_->descriptor_pool, impl_->descriptor_set_layout, frame_count);
    impl_->frame_buffers.reserve(frame_count);

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        impl_->frame_buffers.emplace_back(impl_->allocator, BufferDesc{
            .size = sizeof(GpuCompositeFrame),
            .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        });
        const auto buffer_info = descriptor_buffer_info(impl_->frame_buffers.back());
        const std::array image_infos{
            descriptor_image_info(render_graph_.image(DenoisePass::diffuse_resource, frame)),
            descriptor_image_info(render_graph_.image(DenoisePass::specular_resource, frame)),
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

auto LightCompositePass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), DenoisePass::pass_name);
    render_graph.set_image_usage(name(), DenoisePass::diffuse_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), DenoisePass::specular_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::base_color_ao_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::normal_roughness_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::depth_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), GeometryPass::emissive_metallic_resource, ImageUsage::FragmentSampled);
    render_graph.set_image_usage(name(), output_resource, ImageUsage::ColorAttachment);
}

auto LightCompositePass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("light composite pass must be initialized before preparation");
    }
    const auto frame = render_graph_.current_frame_index();
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const auto view_projection = scene.camera().projection_matrix(aspect) * scene.camera().view_matrix();
    const GpuCompositeFrame data{
        .inverse_view_projection = glm::inverse(view_projection),
        .camera_position = glm::vec4{scene.camera().position(), 1.0F},
        .viewport = {extent.width, extent.height, 0U, 0U}
    };
    impl_->frame_buffers.at(frame).write(&data, sizeof(data));
}

auto LightCompositePass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("light composite pass must be initialized before recording");
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
