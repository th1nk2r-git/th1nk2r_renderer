#include "render/pass/geometry/geometry_pass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>

#include "gfx/device/memory_allocator.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/culling/culling_pass.hpp"
#include "render/render_graph.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/scene.hpp"

namespace {
    struct alignas(16) GpuGeometryCamera {
        glm::mat4 view_projection{1.0F};
    };

    static_assert(sizeof(GpuGeometryCamera) == 64);

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto supports_format(
        const Device& device,
        vk::Format format,
        vk::FormatFeatureFlags required
    ) -> bool {
        const auto properties =
            device.physical_device().getFormatProperties(format);
        return (properties.optimalTilingFeatures & required) == required;
    }

    auto require_format(
        const Device& device,
        vk::Format format,
        vk::FormatFeatureFlags required,
        const char* purpose
    ) -> void {
        if (!supports_format(device, format, required)) {
            throw std::runtime_error(
                std::string{"device does not support the required "} +
                purpose + " format features"
            );
        }
    }

    auto choose_emissive_format(const Device& device) -> vk::Format {
        constexpr auto required =
            vk::FormatFeatureFlagBits::eColorAttachment |
            vk::FormatFeatureFlagBits::eSampledImage;
        if (supports_format(
                device,
                vk::Format::eB10G11R11UfloatPack32,
                required
            )) {
            return vk::Format::eB10G11R11UfloatPack32;
        }
        require_format(
            device,
            vk::Format::eR16G16B16A16Sfloat,
            required,
            "emissive G-buffer"
        );
        return vk::Format::eR16G16B16A16Sfloat;
    }

    auto create_descriptor_set_layout(
        const Device& device,
        std::span<const vk::DescriptorSetLayoutBinding> bindings
    ) -> vk::raii::DescriptorSetLayout {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_descriptor_set_layouts(
        const Device& device,
        uint32_t texture_capacity
    )
        -> std::vector<vk::raii::DescriptorSetLayout> {
        std::array<vk::DescriptorSetLayoutBinding, 2> camera_bindings{};
        camera_bindings[0]
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex);
        camera_bindings[1]
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex);

        std::array<vk::DescriptorSetLayoutBinding, 3> material_bindings{};
        material_bindings[0]
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        material_bindings[1]
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        material_bindings[2]
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(texture_capacity)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);

        std::vector<vk::raii::DescriptorSetLayout> layouts;
        layouts.reserve(2);
        layouts.push_back(create_descriptor_set_layout(
            device,
            camera_bindings
        ));
        layouts.push_back(create_descriptor_set_layout(
            device,
            material_bindings
        ));
        return layouts;
    }

    auto create_pipeline_layout(
        const Device& device,
        const std::vector<vk::raii::DescriptorSetLayout>& layouts
    ) -> vk::raii::PipelineLayout {
        std::vector<vk::DescriptorSetLayout> handles;
        handles.reserve(layouts.size());
        for (const auto& layout : layouts) {
            handles.push_back(*layout);
        }
        vk::PipelineLayoutCreateInfo create_info{};
        create_info.setSetLayouts(handles);
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
            "./spv/geometry_vertex.spv"
        );
        const auto fragment_shader = create_shader_module(
            device,
            "./spv/geometry_fragment.spv"
        );

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
        desc.layout = &layout;
        desc.color_attachment_formats = {
            render_graph.image(
                GeometryPass::base_color_ao_resource
            ).format(),
            render_graph.image(
                GeometryPass::normal_rm_resource
            ).format(),
            render_graph.image(
                GeometryPass::emissive_resource
            ).format()
        };
        desc.depth_attachment_format = render_graph.image(
            GeometryPass::depth_resource
        ).format();
        desc.vertex_bindings = {vertex_binding};
        desc.vertex_attributes = vertex_attributes;
        desc.depth_test_enable = true;
        desc.depth_write_enable = true;
        desc.depth_compare_op = vk::CompareOp::eLess;
        desc.blend_enable = false;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto create_descriptor_pool(
        const Device& device,
        std::span<const vk::DescriptorPoolSize> pool_sizes,
        uint32_t set_count
    ) -> vk::raii::DescriptorPool {
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(set_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_camera_buffers(
        const MemoryAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(GpuGeometryCamera),
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                    .memory = BufferMemoryUsage::Upload,
                    .persistent_mapping = true
                }
            );
        }
        return buffers;
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

    auto checked_texture_capacity(std::size_t texture_count) -> uint32_t {
        const auto capacity = std::max<std::size_t>(texture_count, 1);
        if (capacity > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("texture count exceeds uint32_t");
        }
        return static_cast<uint32_t>(capacity);
    }

    auto create_material_sampler(const Device& device)
        -> vk::raii::Sampler {
        const auto max_anisotropy = device.physical_device()
            .getProperties()
            .limits
            .maxSamplerAnisotropy;
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setAnisotropyEnable(true)
            .setMaxAnisotropy(max_anisotropy)
            .setCompareEnable(false)
            .setMinLod(0.0F)
            .setMaxLod(std::numeric_limits<float>::max())
            .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }

    auto texture_info(const Texture& texture) -> vk::DescriptorImageInfo {
        return vk::DescriptorImageInfo{
            .imageView = *texture.image_view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
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

}

struct GeometryPass::Impl {
    Impl(
        const MemoryAllocator& memory_allocator,
        const AssetsDB& asset_database
    ) : allocator(memory_allocator), assets(asset_database) {}

    const MemoryAllocator& allocator;
    const AssetsDB& assets;

    std::vector<vk::raii::DescriptorSetLayout> descriptor_set_layouts;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    vk::raii::DescriptorPool camera_descriptor_pool = nullptr;
    std::vector<Buffer> camera_buffers;
    std::vector<vk::raii::DescriptorSet> camera_descriptor_sets;

    vk::raii::DescriptorPool material_descriptor_pool = nullptr;
    vk::raii::Sampler material_sampler = nullptr;
    vk::raii::DescriptorSet material_descriptor_set = nullptr;

    std::vector<CommandSlot> command_slots;
    bool initialized = false;
};

GeometryPass::GeometryPass(
    const Device& device,
    const MemoryAllocator& allocator,
    RenderGraph& render_graph,
    const AssetsDB& assets
) : RenderPass(std::string{pass_name}, device, render_graph),
    impl_(std::make_unique<Impl>(allocator, assets)) {}

GeometryPass::~GeometryPass() = default;

auto GeometryPass::declare_resources(
    const Device& device,
    RenderGraph& render_graph,
    vk::Extent2D extent
) -> void {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument(
            "geometry pass resources require a non-zero extent"
        );
    }

    constexpr auto color_features =
        vk::FormatFeatureFlagBits::eColorAttachment |
        vk::FormatFeatureFlagBits::eSampledImage;
    require_format(
        device,
        vk::Format::eR8G8B8A8Srgb,
        color_features,
        "base color G-buffer"
    );
    require_format(
        device,
        vk::Format::eR16G16B16A16Sfloat,
        color_features,
        "normal G-buffer"
    );
    require_format(
        device,
        vk::Format::eD32Sfloat,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment |
            vk::FormatFeatureFlagBits::eSampledImage,
        "depth G-buffer"
    );

    const vk::Extent3D image_extent{extent.width, extent.height, 1};
    const auto declare = [&render_graph, image_extent](
        std::string_view name,
        vk::Format format
    ) {
        render_graph.create_image(
            std::string{name},
            ImageDesc{
                .format = format,
                .extent = image_extent,
                .samples = vk::SampleCountFlagBits::e1
            },
            ResourceMultiplicity::PerFrame
        );
    };

    declare(base_color_ao_resource, vk::Format::eR8G8B8A8Srgb);
    declare(normal_rm_resource, vk::Format::eR16G16B16A16Sfloat);
    declare(emissive_resource, choose_emissive_format(device));
    declare(depth_resource, vk::Format::eD32Sfloat);
}

auto GeometryPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("geometry pass is already initialized");
    }

    const auto texture_count = impl_->assets.texture_count();
    const auto texture_capacity = checked_texture_capacity(texture_count);
    const auto& limits = device_.physical_device().getProperties().limits;
    if (texture_capacity > limits.maxPerStageDescriptorSampledImages ||
        texture_capacity > limits.maxDescriptorSetSampledImages) {
        throw std::length_error(
            "texture count exceeds sampled image descriptor limits"
        );
    }

    impl_->descriptor_set_layouts = create_descriptor_set_layouts(
        device_,
        texture_capacity
    );
    impl_->pipeline_layout = create_pipeline_layout(
        device_,
        impl_->descriptor_set_layouts
    );
    impl_->pipeline = create_pipeline(
        device_,
        render_graph_,
        impl_->pipeline_layout
    );

    const auto frame_count = render_graph_.frames_in_flight_count();
    const std::array camera_pool_sizes{
        vk::DescriptorPoolSize{
            vk::DescriptorType::eUniformBuffer,
            frame_count
        },
        vk::DescriptorPoolSize{
            vk::DescriptorType::eStorageBuffer,
            frame_count
        }
    };
    impl_->camera_descriptor_pool = create_descriptor_pool(
        device_,
        camera_pool_sizes,
        frame_count
    );
    impl_->camera_buffers = create_camera_buffers(
        impl_->allocator,
        frame_count
    );
    impl_->camera_descriptor_sets = allocate_descriptor_sets(
        device_,
        impl_->camera_descriptor_pool,
        impl_->descriptor_set_layouts.at(0),
        frame_count
    );
    for (uint32_t index = 0; index < frame_count; ++index) {
        const GpuGeometryCamera initial_camera{};
        impl_->camera_buffers[index].write(
            &initial_camera,
            sizeof(initial_camera)
        );
        const auto& instance_buffer = render_graph_.buffer(
            CullingPass::instance_resource,
            index
        );
        const std::array buffer_infos{
            vk::DescriptorBufferInfo{
                .buffer = impl_->camera_buffers[index].get(),
                .offset = 0,
                .range = sizeof(GpuGeometryCamera)
            },
            vk::DescriptorBufferInfo{
                .buffer = instance_buffer.get(),
                .offset = 0,
                .range = instance_buffer.size()
            }
        };
        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0]
            .setDstSet(*impl_->camera_descriptor_sets[index])
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(buffer_infos[0]);
        writes[1]
            .setDstSet(*impl_->camera_descriptor_sets[index])
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(buffer_infos[1]);
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    const auto material_count = impl_->assets.material_count();
    if (material_count > 0) {
        if (texture_count == 0) {
            throw std::logic_error(
                "materials require at least one registered texture"
            );
        }
        if (impl_->assets.material_buffer().size() >
            limits.maxStorageBufferRange) {
            throw std::length_error(
                "global material buffer exceeds maxStorageBufferRange"
            );
        }

        const std::array material_pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eStorageBuffer,
                1
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                1
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                texture_capacity
            }
        };
        impl_->material_descriptor_pool = create_descriptor_pool(
            device_,
            material_pool_sizes,
            1
        );
        impl_->material_sampler = create_material_sampler(device_);
        auto material_sets = allocate_descriptor_sets(
            device_,
            impl_->material_descriptor_pool,
            impl_->descriptor_set_layouts.at(1),
            1
        );
        impl_->material_descriptor_set = std::move(material_sets.front());

        const vk::DescriptorImageInfo sampler_info{
            .sampler = *impl_->material_sampler
        };
        const vk::DescriptorBufferInfo material_info{
            .buffer = impl_->assets.material_buffer().get(),
            .offset = 0,
            .range = impl_->assets.material_buffer().size()
        };
        std::vector<vk::DescriptorImageInfo> texture_infos;
        texture_infos.reserve(texture_count);
        // Array slot i is the texture whose ResourceId value is i.
        for (uint32_t index = 0;
             index < static_cast<uint32_t>(texture_count);
             ++index) {
            texture_infos.push_back(texture_info(
                impl_->assets.query(ResourceId<Texture>{index})
            ));
        }

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0]
            .setDstSet(*impl_->material_descriptor_set)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(material_info);
        writes[1]
            .setDstSet(*impl_->material_descriptor_set)
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info);
        writes[2]
            .setDstSet(*impl_->material_descriptor_set)
            .setDstBinding(2)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setImageInfo(texture_infos);
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->command_slots = create_command_slots(device_, frame_count);
    impl_->initialized = true;
}

auto GeometryPass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), CullingPass::pass_name);
    render_graph.set_buffer_usage(
        name(),
        CullingPass::instance_resource,
        BufferUsage::VertexStorageRead
    );
    render_graph.set_buffer_usage(
        name(),
        CullingPass::command_resource,
        BufferUsage::Indirect
    );
    render_graph.set_buffer_usage(
        name(),
        CullingPass::count_resource,
        BufferUsage::Indirect
    );
    render_graph.set_image_usage(
        name(),
        base_color_ao_resource,
        ImageUsage::ColorAttachment
    );
    render_graph.set_image_usage(
        name(),
        normal_rm_resource,
        ImageUsage::ColorAttachment
    );
    render_graph.set_image_usage(
        name(),
        emissive_resource,
        ImageUsage::ColorAttachment
    );
    render_graph.set_image_usage(
        name(),
        depth_resource,
        ImageUsage::DepthAttachment
    );
}

auto GeometryPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error(
            "geometry pass must be initialized before preparation"
        );
    }

    const auto extent = render_graph_.image(
        base_color_ao_resource
    ).extent();
    const auto aspect_ratio = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const auto view = scene.camera().view_matrix();
    const auto projection =
        scene.camera().projection_matrix(aspect_ratio);
    const auto view_projection = projection * view;
    const GpuGeometryCamera camera{
        .view_projection = view_projection
    };
    impl_->camera_buffers.at(
        render_graph_.current_frame_index()
    ).write(&camera, sizeof(camera));
}

auto GeometryPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error(
            "geometry pass must be initialized before recording"
        );
    }

    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->command_slots.at(frame_index);
    slot.pool.reset();

    const std::array color_formats{
        render_graph_.image(base_color_ao_resource).format(),
        render_graph_.image(normal_rm_resource).format(),
        render_graph_.image(emissive_resource).format()
    };
    vk::CommandBufferInheritanceRenderingInfo rendering_inheritance{};
    rendering_inheritance
        .setColorAttachmentFormats(color_formats)
        .setDepthAttachmentFormat(
            render_graph_.image(depth_resource).format()
        )
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

    const auto extent_3d = render_graph_.image(
        base_color_ao_resource
    ).extent();
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

    const std::array camera_sets{
        *impl_->camera_descriptor_sets.at(frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *impl_->pipeline_layout,
        0,
        camera_sets,
        {}
    );

    if (impl_->assets.material_count() > 0) {
        const std::array vertex_buffers{
            impl_->assets.vertex_buffer().get()
        };
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(
            0,
            vertex_buffers,
            vertex_offsets
        );
        command_buffer.bindIndexBuffer(
            impl_->assets.index_buffer().get(),
            0,
            vk::IndexType::eUint32
        );

        const std::array material_sets{*impl_->material_descriptor_set};
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *impl_->pipeline_layout,
            1,
            material_sets,
            {}
        );
        command_buffer.drawIndexedIndirectCount(
            render_graph_.buffer(CullingPass::command_resource).get(),
            0,
            render_graph_.buffer(CullingPass::count_resource).get(),
            0,
            CullingPass::max_instance_count,
            sizeof(vk::DrawIndexedIndirectCommand)
        );
    }

    command_buffer.end();
    return *command_buffer;
}
