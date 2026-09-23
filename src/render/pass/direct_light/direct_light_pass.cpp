#include "render/pass/direct_light/direct_light_pass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include "gfx/device/memory_allocator.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/render_graph.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t gbuffer_texture_count = 4;

    struct alignas(16) GpuDirectLightFrame {
        glm::mat4 inverse_view_projection{1.0F};
        glm::vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
        glm::uvec4 light_count_viewport{0U};
    };

    struct alignas(16) GpuDirectLightPointLight {
        glm::vec4 position_range{0.0F};
        glm::vec4 color_intensity{0.0F};
        glm::vec4 radiance{0.0F};
    };

    static_assert(sizeof(GpuDirectLightFrame) == 96);
    static_assert(alignof(GpuDirectLightFrame) == 16);
    static_assert(offsetof(GpuDirectLightFrame, inverse_view_projection) == 0);
    static_assert(offsetof(GpuDirectLightFrame, camera_position) == 64);
    static_assert(offsetof(GpuDirectLightFrame, light_count_viewport) == 80);
    static_assert(sizeof(GpuDirectLightPointLight) == 48);
    static_assert(alignof(GpuDirectLightPointLight) == 16);
    static_assert(offsetof(GpuDirectLightPointLight, position_range) == 0);
    static_assert(offsetof(GpuDirectLightPointLight, color_intensity) == 16);
    static_assert(offsetof(GpuDirectLightPointLight, radiance) == 32);

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto create_descriptor_set_layout(const Device& device)
        -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{
                0,
                vk::DescriptorType::eUniformBuffer,
                1,
                vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                1,
                vk::DescriptorType::eStorageBuffer,
                1,
                vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                2,
                vk::DescriptorType::eSampledImage,
                1,
                vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                3,
                vk::DescriptorType::eSampledImage,
                1,
                vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                4,
                vk::DescriptorType::eSampledImage,
                1,
                vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                5,
                vk::DescriptorType::eSampledImage,
                1,
                vk::ShaderStageFlagBits::eFragment
            }
        };
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout
    ) -> vk::raii::PipelineLayout {
        const std::array layouts{*descriptor_set_layout};
        vk::PipelineLayoutCreateInfo create_info{};
        create_info.setSetLayouts(layouts);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path
    ) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        const vk::ShaderModuleCreateInfo create_info{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()
        };
        return device.logical_device().createShaderModule(create_info);
    }

    auto create_pipeline(
        const Device& device,
        const RenderGraph& render_graph,
        const vk::raii::PipelineLayout& layout
    ) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(
            device,
            "./spv/direct_light_vertex.spv"
        );
        const auto fragment_shader = create_shader_module(
            device,
            "./spv/direct_light_fragment.spv"
        );

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &layout;
        desc.color_attachment_formats = {
            render_graph.image(DirectLightPass::output_resource).format()
        };
        desc.cull_mode = vk::CullModeFlagBits::eNone;
        desc.depth_test_enable = false;
        desc.depth_write_enable = false;
        desc.blend_enable = false;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        if (frame_count >
            std::numeric_limits<uint32_t>::max() / gbuffer_texture_count) {
            throw std::overflow_error(
                "direct-light descriptor count exceeds uint32_t"
            );
        }
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer,
                frame_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eStorageBuffer,
                frame_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                frame_count * gbuffer_texture_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto allocate_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& pool,
        const vk::raii::DescriptorSetLayout& layout,
        uint32_t count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(count, *layout);
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*pool)
            .setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(allocate_info);
    }

    auto create_frame_buffers(
        const MemoryAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(GpuDirectLightFrame),
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                    .memory = BufferMemoryUsage::Upload,
                    .persistent_mapping = true
                }
            );
        }
        return buffers;
    }

    auto create_light_buffers(
        const MemoryAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(GpuDirectLightPointLight),
                    .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                    .memory = BufferMemoryUsage::Upload,
                    .persistent_mapping = true
                }
            );
        }
        return buffers;
    }

    auto create_command_slots(
        const Device& device,
        uint32_t frame_count
    ) -> std::vector<CommandSlot> {
        std::vector<CommandSlot> slots;
        slots.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            auto pool = device.logical_device().createCommandPool(
                vk::CommandPoolCreateInfo{
                    .flags = vk::CommandPoolCreateFlagBits::eTransient,
                    .queueFamilyIndex = device.graphics_family()
                }
            );
            const vk::CommandBufferAllocateInfo allocate_info{
                .commandPool = *pool,
                .level = vk::CommandBufferLevel::eSecondary,
                .commandBufferCount = 1
            };
            auto command_buffers =
                device.logical_device().allocateCommandBuffers(allocate_info);
            slots.push_back(CommandSlot{
                .pool = std::move(pool),
                .command_buffer = std::move(command_buffers.front())
            });
        }
        return slots;
    }

    auto descriptor_image_info(const Image& image)
        -> vk::DescriptorImageInfo {
        return {
            .imageView = *image.view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
    }

    auto light_buffer_size(std::size_t capacity) -> vk::DeviceSize {
        if (capacity == 0) {
            throw std::invalid_argument(
                "direct-light buffer capacity must be greater than zero"
            );
        }
        if (capacity >
            std::numeric_limits<vk::DeviceSize>::max() /
                sizeof(GpuDirectLightPointLight)) {
            throw std::overflow_error("direct-light buffer size overflow");
        }
        return static_cast<vk::DeviceSize>(capacity) *
            sizeof(GpuDirectLightPointLight);
    }

    auto gpu_light(const PointLight& light) -> GpuDirectLightPointLight {
        return {
            .position_range = glm::vec4{light.position, light.range},
            .color_intensity = glm::vec4{light.color, light.intensity},
            .radiance = glm::vec4{light.radiance, 0.0F, 0.0F, 0.0F}
        };
    }
}

struct DirectLightPass::Impl {
    explicit Impl(const MemoryAllocator& memory_allocator)
        : allocator(memory_allocator) {}

    const MemoryAllocator& allocator;

    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<Buffer> frame_buffers;
    std::vector<Buffer> light_buffers;
    std::vector<std::size_t> light_buffer_capacities;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;

    std::vector<CommandSlot> command_slots;
    std::vector<GpuDirectLightPointLight> gpu_lights;
    bool initialized = false;
};

DirectLightPass::DirectLightPass(
    const Device& device,
    const MemoryAllocator& allocator,
    RenderGraph& render_graph
) : RenderPass(std::string{pass_name}, device, render_graph),
    impl_(std::make_unique<Impl>(allocator)) {}

DirectLightPass::~DirectLightPass() = default;

auto DirectLightPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("direct-light pass is already initialized");
    }

    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    impl_->pipeline_layout = create_pipeline_layout(
        device_,
        impl_->descriptor_set_layout
    );
    impl_->pipeline = create_pipeline(
        device_,
        render_graph_,
        impl_->pipeline_layout
    );

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->descriptor_pool = create_descriptor_pool(device_, frame_count);
    impl_->frame_buffers = create_frame_buffers(
        impl_->allocator,
        frame_count
    );
    impl_->light_buffers = create_light_buffers(
        impl_->allocator,
        frame_count
    );
    impl_->light_buffer_capacities.assign(frame_count, 1);
    impl_->descriptor_sets = allocate_descriptor_sets(
        device_,
        impl_->descriptor_pool,
        impl_->descriptor_set_layout,
        frame_count
    );

    for (uint32_t index = 0; index < frame_count; ++index) {
        const GpuDirectLightFrame initial_frame{};
        const GpuDirectLightPointLight initial_light{};
        impl_->frame_buffers[index].write(
            &initial_frame,
            sizeof(initial_frame)
        );
        impl_->light_buffers[index].write(
            &initial_light,
            sizeof(initial_light)
        );

        const vk::DescriptorBufferInfo frame_info{
            .buffer = impl_->frame_buffers[index].get(),
            .offset = 0,
            .range = sizeof(GpuDirectLightFrame)
        };
        const vk::DescriptorBufferInfo light_info{
            .buffer = impl_->light_buffers[index].get(),
            .offset = 0,
            .range = impl_->light_buffers[index].size()
        };
        const std::array image_infos{
            descriptor_image_info(render_graph_.image(
                GeometryPass::base_color_ao_resource,
                index
            )),
            descriptor_image_info(render_graph_.image(
                GeometryPass::normal_rm_resource,
                index
            )),
            descriptor_image_info(render_graph_.image(
                GeometryPass::emissive_resource,
                index
            )),
            descriptor_image_info(render_graph_.image(
                GeometryPass::depth_resource,
                index
            ))
        };

        std::array<vk::WriteDescriptorSet, 6> writes{};
        writes[0]
            .setDstSet(*impl_->descriptor_sets[index])
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(frame_info);
        writes[1]
            .setDstSet(*impl_->descriptor_sets[index])
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(light_info);
        for (uint32_t texture = 0; texture < gbuffer_texture_count;
             ++texture) {
            writes[texture + 2]
                .setDstSet(*impl_->descriptor_sets[index])
                .setDstBinding(texture + 2)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(image_infos[texture]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->command_slots = create_command_slots(device_, frame_count);
    impl_->initialized = true;
}

auto DirectLightPass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), GeometryPass::pass_name);
    render_graph.set_image_usage(
        name(),
        GeometryPass::base_color_ao_resource,
        ImageUsage::FragmentSampled
    );
    render_graph.set_image_usage(
        name(),
        GeometryPass::normal_rm_resource,
        ImageUsage::FragmentSampled
    );
    render_graph.set_image_usage(
        name(),
        GeometryPass::emissive_resource,
        ImageUsage::FragmentSampled
    );
    render_graph.set_image_usage(
        name(),
        GeometryPass::depth_resource,
        ImageUsage::FragmentSampled
    );
    render_graph.set_image_usage(
        name(),
        output_resource,
        ImageUsage::ColorAttachment
    );
}

auto DirectLightPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error(
            "direct-light pass must be initialized before preparation"
        );
    }

    const auto& lights = scene.point_lights();
    if (lights.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("point-light count exceeds uint32_t");
    }

    const auto frame_index = render_graph_.current_frame_index();
    const auto required_capacity = std::max<std::size_t>(lights.size(), 1);
    if (required_capacity >
        impl_->light_buffer_capacities.at(frame_index)) {
        const auto required_size = light_buffer_size(required_capacity);
        const auto max_range = device_.physical_device()
            .getProperties().limits.maxStorageBufferRange;
        if (required_size > max_range) {
            throw std::length_error(
                "point-light buffer exceeds maxStorageBufferRange"
            );
        }

        impl_->light_buffers.at(frame_index) = Buffer{
            impl_->allocator,
            BufferDesc{
                .size = required_size,
                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                .memory = BufferMemoryUsage::Upload,
                .persistent_mapping = true
            }
        };
        impl_->light_buffer_capacities.at(frame_index) = required_capacity;

        const vk::DescriptorBufferInfo light_info{
            .buffer = impl_->light_buffers.at(frame_index).get(),
            .offset = 0,
            .range = required_size
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*impl_->descriptor_sets.at(frame_index))
                .setDstBinding(1)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(light_info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->gpu_lights.clear();
    impl_->gpu_lights.reserve(lights.size());
    for (const auto& light : lights) {
        impl_->gpu_lights.push_back(gpu_light(light));
    }
    if (!impl_->gpu_lights.empty()) {
        impl_->light_buffers.at(frame_index).write(
            impl_->gpu_lights.data(),
            light_buffer_size(impl_->gpu_lights.size())
        );
    }

    const auto extent = render_graph_.image(
        GeometryPass::depth_resource
    ).extent();
    const auto aspect_ratio = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const auto view_projection =
        scene.camera().projection_matrix(aspect_ratio) *
        scene.camera().view_matrix();
    const GpuDirectLightFrame frame{
        .inverse_view_projection = glm::inverse(view_projection),
        .camera_position = glm::vec4{scene.camera().position(), 1.0F},
        .light_count_viewport = {
            static_cast<uint32_t>(lights.size()),
            extent.width,
            extent.height,
            0U
        }
    };
    impl_->frame_buffers.at(frame_index).write(&frame, sizeof(frame));
}

auto DirectLightPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error(
            "direct-light pass must be initialized before recording"
        );
    }

    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->command_slots.at(frame_index);
    slot.pool.reset();

    const std::array color_formats{
        render_graph_.image(output_resource).format()
    };
    vk::CommandBufferInheritanceRenderingInfo rendering_inheritance{};
    rendering_inheritance
        .setColorAttachmentFormats(color_formats)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::CommandBufferInheritanceInfo inheritance{};
    inheritance.setPNext(&rendering_inheritance);
    vk::CommandBufferBeginInfo begin_info{};
    begin_info
        .setFlags(
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
            vk::CommandBufferUsageFlagBits::eRenderPassContinue
        )
        .setPInheritanceInfo(&inheritance);

    auto& command_buffer = slot.command_buffer;
    command_buffer.begin(begin_info);
    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *impl_->pipeline
    );

    const auto extent_3d = render_graph_.image(output_resource).extent();
    const vk::Extent2D extent{extent_3d.width, extent_3d.height};
    command_buffer.setViewport(
        0,
        vk::Viewport{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F
        }
    );
    command_buffer.setScissor(
        0,
        vk::Rect2D{
            .offset = vk::Offset2D{0, 0},
            .extent = extent
        }
    );

    const std::array descriptor_sets{
        *impl_->descriptor_sets.at(frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *impl_->pipeline_layout,
        0,
        descriptor_sets,
        {}
    );
    command_buffer.draw(3, 1, 0, 0);
    command_buffer.end();
    return *command_buffer;
}
