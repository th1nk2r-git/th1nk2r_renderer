#include "render/pass/geometry/geometry_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "gfx/device/memory_allocator.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/render_graph.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t material_texture_count = 5;

    struct alignas(16) GpuGeometryCamera {
        glm::mat4 view_projection{1.0F};
    };

    struct alignas(16) GpuGeometryMaterial {
        glm::vec4 base_color_factor{1.0F};
        glm::vec4 emissive_normal_scale{0.0F, 0.0F, 0.0F, 1.0F};
        glm::vec4 metallic_roughness_occlusion_alpha_cutoff{
            0.0F, 1.0F, 1.0F, 0.5F
        };
        glm::uvec4 flags{0U};
    };

    struct alignas(16) GpuGeometryDraw {
        glm::mat4 model{1.0F};
        glm::vec4 normal_column_0{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_1{0.0F, 1.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_2{0.0F, 0.0F, 1.0F, 0.0F};
    };

    static_assert(sizeof(GpuGeometryCamera) == 64);
    static_assert(sizeof(GpuGeometryMaterial) == 64);
    static_assert(sizeof(GpuGeometryDraw) == 112);

    struct DrawItem {
        ResourceId<Mesh> mesh;
        ResourceId<Material> material;
        GpuGeometryDraw constants;
    };

    struct FrustumPlane {
        glm::vec3 normal{0.0F};
        float offset = 0.0F;
    };

    using Frustum = std::array<FrustumPlane, 6>;

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto matrix_row(const glm::mat4& matrix, std::size_t row) noexcept
        -> glm::vec4 {
        return {
            matrix[0][row],
            matrix[1][row],
            matrix[2][row],
            matrix[3][row]
        };
    }

    auto make_plane(const glm::vec4& coefficients) noexcept
        -> FrustumPlane {
        return {
            .normal = glm::vec3{coefficients},
            .offset = coefficients.w
        };
    }

    auto make_frustum(const glm::mat4& view_projection) noexcept
        -> Frustum {
        const auto row_0 = matrix_row(view_projection, 0);
        const auto row_1 = matrix_row(view_projection, 1);
        const auto row_2 = matrix_row(view_projection, 2);
        const auto row_3 = matrix_row(view_projection, 3);

        // Vulkan clip space uses -w <= x,y <= w and 0 <= z <= w.
        return {
            make_plane(row_3 + row_0),
            make_plane(row_3 - row_0),
            make_plane(row_3 + row_1),
            make_plane(row_3 - row_1),
            make_plane(row_2),
            make_plane(row_3 - row_2)
        };
    }

    auto intersects(
        const Frustum& frustum,
        const Mesh::Bounds& local_bounds,
        const glm::mat4& model
    ) noexcept -> bool {
        const auto local_center =
            (local_bounds.minimum + local_bounds.maximum) * 0.5F;
        const auto local_extents =
            (local_bounds.maximum - local_bounds.minimum) * 0.5F;
        const auto world_center = glm::vec3{
            model * glm::vec4{local_center, 1.0F}
        };

        const glm::vec3 world_extents{
            std::abs(model[0][0]) * local_extents.x +
                std::abs(model[1][0]) * local_extents.y +
                std::abs(model[2][0]) * local_extents.z,
            std::abs(model[0][1]) * local_extents.x +
                std::abs(model[1][1]) * local_extents.y +
                std::abs(model[2][1]) * local_extents.z,
            std::abs(model[0][2]) * local_extents.x +
                std::abs(model[1][2]) * local_extents.y +
                std::abs(model[2][2]) * local_extents.z
        };

        for (const auto& plane : frustum) {
            const auto radius =
                std::abs(plane.normal.x) * world_extents.x +
                std::abs(plane.normal.y) * world_extents.y +
                std::abs(plane.normal.z) * world_extents.z;
            const auto distance =
                glm::dot(plane.normal, world_center) + plane.offset;
            if (distance + radius < 0.0F) {
                return false;
            }
        }
        return true;
    }

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

    auto create_descriptor_set_layouts(const Device& device)
        -> std::vector<vk::raii::DescriptorSetLayout> {
        const std::array camera_bindings{
            vk::DescriptorSetLayoutBinding{
                0,
                vk::DescriptorType::eUniformBuffer,
                1,
                vk::ShaderStageFlagBits::eVertex
            }
        };

        std::array<vk::DescriptorSetLayoutBinding, 7> material_bindings{};
        material_bindings[0]
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        for (uint32_t binding = 1; binding <= material_texture_count;
             ++binding) {
            material_bindings[binding]
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        }
        material_bindings[6]
            .setBinding(6)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
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
        if (sizeof(GpuGeometryDraw) >
            device.physical_device().getProperties().limits.maxPushConstantsSize) {
            throw std::runtime_error(
                "geometry draw constants exceed maxPushConstantsSize"
            );
        }

        std::vector<vk::DescriptorSetLayout> handles;
        handles.reserve(layouts.size());
        for (const auto& layout : layouts) {
            handles.push_back(*layout);
        }
        const std::array push_ranges{
            vk::PushConstantRange{
                vk::ShaderStageFlagBits::eVertex,
                0,
                sizeof(GpuGeometryDraw)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(handles)
            .setPushConstantRanges(push_ranges);
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

    auto align_up(vk::DeviceSize value, vk::DeviceSize alignment)
        -> vk::DeviceSize {
        if (alignment <= 1) {
            return value;
        }
        if (value >
            std::numeric_limits<vk::DeviceSize>::max() - alignment + 1) {
            throw std::overflow_error("material buffer alignment overflow");
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    auto material_parameter_stride(const Device& device) -> vk::DeviceSize {
        const auto limits = device.physical_device().getProperties().limits;
        if (sizeof(GpuGeometryMaterial) > limits.maxUniformBufferRange) {
            throw std::runtime_error(
                "geometry material parameters exceed maxUniformBufferRange"
            );
        }
        return align_up(
            sizeof(GpuGeometryMaterial),
            limits.minUniformBufferOffsetAlignment
        );
    }

    auto checked_material_capacity(std::size_t material_count) -> uint32_t {
        const auto capacity = std::max<std::size_t>(material_count, 1);
        if (capacity > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("material count exceeds uint32_t");
        }
        return static_cast<uint32_t>(capacity);
    }

    auto checked_buffer_size(
        vk::DeviceSize stride,
        uint32_t count
    ) -> vk::DeviceSize {
        if (stride > std::numeric_limits<vk::DeviceSize>::max() / count) {
            throw std::overflow_error("material parameter buffer overflow");
        }
        return stride * count;
    }

    auto create_material_sampler(const Device& device)
        -> vk::raii::Sampler {
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setAnisotropyEnable(false)
            .setMaxAnisotropy(1.0F)
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

    auto gpu_material(const Material& material) -> GpuGeometryMaterial {
        const auto& base_color = material.base_color_factor();
        const auto& emissive = material.emissive_color();
        return {
            .base_color_factor = {
                base_color[0],
                base_color[1],
                base_color[2],
                base_color[3]
            },
            .emissive_normal_scale = {
                emissive[0],
                emissive[1],
                emissive[2],
                material.normal_scale()
            },
            .metallic_roughness_occlusion_alpha_cutoff = {
                material.metallic(),
                material.roughness(),
                material.occlusion_strength(),
                material.alpha_cutoff()
            },
            .flags = {
                material.alpha_mask() ? 1U : 0U,
                0U,
                0U,
                0U
            }
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

    auto draw_constants(const glm::mat4& model) -> GpuGeometryDraw {
        const auto normal_matrix = glm::transpose(
            glm::inverse(glm::mat3{model})
        );
        return {
            .model = model,
            .normal_column_0 = glm::vec4{normal_matrix[0], 0.0F},
            .normal_column_1 = glm::vec4{normal_matrix[1], 0.0F},
            .normal_column_2 = glm::vec4{normal_matrix[2], 0.0F}
        };
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
    Buffer material_parameter_buffer;
    vk::raii::Sampler material_sampler = nullptr;
    std::vector<vk::raii::DescriptorSet> material_descriptor_sets;
    vk::DeviceSize material_parameter_stride = 0;

    std::vector<CommandSlot> command_slots;
    std::vector<DrawItem> draw_items;
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

    impl_->descriptor_set_layouts =
        create_descriptor_set_layouts(device_);
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
        const vk::DescriptorBufferInfo buffer_info{
            .buffer = impl_->camera_buffers[index].get(),
            .offset = 0,
            .range = sizeof(GpuGeometryCamera)
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*impl_->camera_descriptor_sets[index])
                .setDstBinding(0)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setBufferInfo(buffer_info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    const auto material_count = impl_->assets.material_count();
    const auto material_capacity =
        checked_material_capacity(material_count);
    if (material_capacity >
        std::numeric_limits<uint32_t>::max() / material_texture_count) {
        throw std::overflow_error(
            "geometry material descriptor count exceeds uint32_t"
        );
    }
    const std::array material_pool_sizes{
        vk::DescriptorPoolSize{
            vk::DescriptorType::eSampler,
            material_capacity
        },
        vk::DescriptorPoolSize{
            vk::DescriptorType::eSampledImage,
            material_capacity * material_texture_count
        },
        vk::DescriptorPoolSize{
            vk::DescriptorType::eUniformBuffer,
            material_capacity
        }
    };
    impl_->material_descriptor_pool = create_descriptor_pool(
        device_,
        material_pool_sizes,
        material_capacity
    );
    impl_->material_parameter_stride =
        material_parameter_stride(device_);
    impl_->material_parameter_buffer = Buffer{
        impl_->allocator,
        BufferDesc{
            .size = checked_buffer_size(
                impl_->material_parameter_stride,
                material_capacity
            ),
            .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }
    };
    impl_->material_sampler = create_material_sampler(device_);

    if (material_count > 0) {
        impl_->material_descriptor_sets = allocate_descriptor_sets(
            device_,
            impl_->material_descriptor_pool,
            impl_->descriptor_set_layouts.at(1),
            static_cast<uint32_t>(material_count)
        );
    }
    for (uint32_t index = 0; index < static_cast<uint32_t>(material_count); ++index) {
        const auto id = ResourceId<Material>{index};
        const auto& material = impl_->assets.query(id);
        const auto parameters = gpu_material(material);
        const auto parameter_offset =
            impl_->material_parameter_stride * index;
        impl_->material_parameter_buffer.write(
            &parameters,
            sizeof(parameters),
            parameter_offset
        );

        const vk::DescriptorImageInfo sampler_info{
            .sampler = *impl_->material_sampler
        };
        const std::array texture_infos{
            texture_info(impl_->assets.query(
                material.base_color_texture_id()
            )),
            texture_info(impl_->assets.query(
                material.metallic_roughness_texture_id()
            )),
            texture_info(impl_->assets.query(
                material.normal_texture_id()
            )),
            texture_info(impl_->assets.query(
                material.occlusion_texture_id()
            )),
            texture_info(impl_->assets.query(
                material.emissive_texture_id()
            ))
        };
        const vk::DescriptorBufferInfo parameter_info{
            .buffer = impl_->material_parameter_buffer.get(),
            .offset = parameter_offset,
            .range = sizeof(GpuGeometryMaterial)
        };

        std::array<vk::WriteDescriptorSet, 7> writes{};
        writes[0]
            .setDstSet(*impl_->material_descriptor_sets[index])
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info);
        for (uint32_t texture = 0; texture < material_texture_count; ++texture) {
            writes[texture + 1]
                .setDstSet(*impl_->material_descriptor_sets[index])
                .setDstBinding(texture + 1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(texture_infos[texture]);
        }
        writes[6]
            .setDstSet(*impl_->material_descriptor_sets[index])
            .setDstBinding(6)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(parameter_info);
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->command_slots = create_command_slots(device_, frame_count);
    impl_->initialized = true;
}

auto GeometryPass::configure(RenderGraph& render_graph) -> void {
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

    const auto frustum = make_frustum(view_projection);
    impl_->draw_items.clear();
    for (const auto& entity : scene.entities()) {
        const auto* mesh_renderer =
            entity.get_component<MeshRenderer>();
        if (mesh_renderer == nullptr) {
            continue;
        }
        const auto* transform = entity.get_component<Transform>();
        const auto model = transform == nullptr
            ? glm::mat4{1.0F}
            : transform->model_matrix();
        const auto constants = draw_constants(model);
        const auto& render_model = impl_->assets.query(
            mesh_renderer->model_id()
        );
        for (const auto& primitive : render_model.primitives()) {
            const auto& mesh = impl_->assets.query(primitive.mesh);
            if (!intersects(frustum, mesh.bounds, model)) {
                continue;
            }
            impl_->draw_items.push_back(DrawItem{
                .mesh = primitive.mesh,
                .material = primitive.material,
                .constants = constants
            });
        }
    }
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

    if (!impl_->draw_items.empty()) {
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
    }

    auto bound_material = ResourceId<Material>{};
    for (const auto& item : impl_->draw_items) {
        if (item.material != bound_material) {
            const std::array material_sets{
                *impl_->material_descriptor_sets.at(
                    item.material.value()
                )
            };
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *impl_->pipeline_layout,
                1,
                material_sets,
                {}
            );
            bound_material = item.material;
        }

        command_buffer.pushConstants<GpuGeometryDraw>(
            *impl_->pipeline_layout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            item.constants
        );
        const auto& mesh = impl_->assets.query(item.mesh);
        command_buffer.drawIndexed(
            mesh.index_count,
            1,
            mesh.first_index,
            mesh.vertex_offset,
            0
        );
    }

    command_buffer.end();
    return *command_buffer;
}
