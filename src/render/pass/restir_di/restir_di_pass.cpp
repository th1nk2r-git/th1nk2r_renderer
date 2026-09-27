#include "render/pass/restir_di/restir_di_pass.hpp"

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

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/pipeline/compute_pipeline.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/render_graph.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t gbuffer_texture_count = 4;
    constexpr uint32_t thread_group_size = 8;

    struct alignas(16) GpuRestirFrame {
        glm::mat4 inverse_view_projection{1.0F};
        glm::mat4 view_projection{1.0F};
        glm::vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
        glm::uvec4 light_count_viewport{0U};
        glm::uvec4 sampling{0U};
        glm::uvec4 reuse_limits{0U};
    };

    // Mirrors RestirReservoir in shaders/common/restir_di.slangh. This is
    // storage layout, not the number of samples retained by a reservoir.
    struct GpuRestirReservoir {
        uint32_t light_index = 0;
        uint32_t packed_uv = 0;
        float weight = 0.0F;
        float target = 0.0F;
        uint32_t sample_count = 0;
        float depth = 1.0F;
        uint32_t packed_normal_rm = 0;
        float view_depth = 0.0F;
    };

    struct alignas(16) GpuRestirPointLight {
        glm::vec4 position_range{0.0F};
        glm::vec4 color_intensity{0.0F};
        glm::vec4 radius{0.0F};
    };

    static_assert(sizeof(GpuRestirFrame) == 192);
    static_assert(offsetof(GpuRestirFrame, view_projection) == 64);
    static_assert(offsetof(GpuRestirFrame, camera_position) == 128);
    static_assert(offsetof(GpuRestirFrame, light_count_viewport) == 144);
    static_assert(offsetof(GpuRestirFrame, sampling) == 160);
    static_assert(offsetof(GpuRestirFrame, reuse_limits) == 176);
    static_assert(sizeof(GpuRestirReservoir) == 32);
    static_assert(offsetof(GpuRestirReservoir, sample_count) == 16);
    static_assert(offsetof(GpuRestirReservoir, packed_normal_rm) == 24);
    static_assert(sizeof(GpuRestirPointLight) == 48);

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto create_descriptor_set_layout(const Device& device)
        -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eSampledImage,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eSampledImage,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eSampledImage,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eStorageBuffer,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eStorageBuffer,
                1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{7, vk::DescriptorType::eSampledImage,
                1, vk::ShaderStageFlagBits::eCompute}
        };
        vk::DescriptorSetLayoutCreateInfo info{};
        info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(info);
    }

    auto create_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& set_layout
    ) -> vk::raii::PipelineLayout {
        const std::array layouts{*set_layout};
        vk::PipelineLayoutCreateInfo info{};
        info.setSetLayouts(layouts);
        return device.logical_device().createPipelineLayout(info);
    }

    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path
    ) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        const vk::ShaderModuleCreateInfo info{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()
        };
        return device.logical_device().createShaderModule(info);
    }

    auto create_pipeline(
        const Device& device,
        const vk::raii::PipelineLayout& layout,
        const std::filesystem::path& path
    ) -> vk::raii::Pipeline {
        const auto shader = create_shader_module(device, path);
        return ComputePipelineFactory::create(
            device,
            ComputePipelineDesc{.compute_shader = &shader, .layout = &layout}
        );
    }

    auto create_descriptor_pool(const Device& device, uint32_t count)
        -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, count},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, count * 3},
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                count * gbuffer_texture_count
            }
        };
        vk::DescriptorPoolCreateInfo info{};
        info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(info);
    }

    auto allocate_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& pool,
        const vk::raii::DescriptorSetLayout& layout,
        uint32_t count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(count, *layout);
        vk::DescriptorSetAllocateInfo info{};
        info.setDescriptorPool(*pool).setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(info);
    }

    auto create_upload_buffers(
        const MemoryAllocator& allocator,
        uint32_t count,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage
    ) -> std::vector<Buffer> {
        std::vector<Buffer> result;
        result.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            result.emplace_back(allocator, BufferDesc{
                .size = size,
                .usage = usage,
                .memory = BufferMemoryUsage::Upload,
                .persistent_mapping = true
            });
        }
        return result;
    }

    auto create_command_slots(const Device& device, uint32_t count)
        -> std::vector<CommandSlot> {
        std::vector<CommandSlot> result;
        result.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            auto pool = device.logical_device().createCommandPool(
                vk::CommandPoolCreateInfo{
                    .flags = vk::CommandPoolCreateFlagBits::eTransient,
                    .queueFamilyIndex = device.graphics_family()
                }
            );
            const vk::CommandBufferAllocateInfo info{
                .commandPool = *pool,
                .level = vk::CommandBufferLevel::eSecondary,
                .commandBufferCount = 1
            };
            auto buffers = device.logical_device().allocateCommandBuffers(info);
            result.push_back(CommandSlot{
                .pool = std::move(pool),
                .command_buffer = std::move(buffers.front())
            });
        }
        return result;
    }

    auto image_info(const Image& image) -> vk::DescriptorImageInfo {
        return {
            .imageView = *image.view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
    }

    auto light_buffer_size(std::size_t capacity) -> vk::DeviceSize {
        if (capacity > std::numeric_limits<vk::DeviceSize>::max() /
            sizeof(GpuRestirPointLight)) {
            throw std::overflow_error("ReSTIR light buffer size overflow");
        }
        return static_cast<vk::DeviceSize>(capacity) *
            sizeof(GpuRestirPointLight);
    }

    auto gpu_light(const PointLight& light) -> GpuRestirPointLight {
        return {
            .position_range = glm::vec4{light.position, light.range},
            .color_intensity = glm::vec4{light.color, light.intensity},
            .radius = glm::vec4{light.radius, 0.0F, 0.0F, 0.0F}
        };
    }

    auto same_light(const PointLight& lhs, const PointLight& rhs) -> bool {
        return lhs.position == rhs.position && lhs.range == rhs.range &&
            lhs.radius == rhs.radius && lhs.color == rhs.color &&
            lhs.intensity == rhs.intensity;
    }
}

struct RestirDiPass::Impl {
    explicit Impl(const MemoryAllocator& memory_allocator)
        : allocator(memory_allocator) {}

    const MemoryAllocator& allocator;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline initial_temporal_pipeline = nullptr;
    vk::raii::Pipeline spatial_pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;
    std::vector<Buffer> frame_buffers;
    std::vector<Buffer> light_buffers;
    std::vector<std::size_t> light_buffer_capacities;
    std::vector<CommandSlot> command_slots;
    std::vector<GpuRestirPointLight> gpu_lights;
    std::vector<PointLight> previous_lights;
    uint32_t frame_sequence = 0;
    bool history_valid = false;
    bool initialized = false;
};

RestirDiPass::RestirDiPass(
    const Device& device,
    const MemoryAllocator& allocator,
    RenderGraph& render_graph
) : RenderPass(std::string{pass_name}, device, render_graph),
    impl_(std::make_unique<Impl>(allocator)) {}

RestirDiPass::~RestirDiPass() = default;

auto RestirDiPass::declare_resources(
    const Device& device,
    RenderGraph& render_graph,
    vk::Extent2D extent
) -> void {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("ReSTIR requires a non-empty viewport");
    }
    const auto pixel_count = static_cast<vk::DeviceSize>(extent.width) *
        static_cast<vk::DeviceSize>(extent.height);
    if (pixel_count > std::numeric_limits<vk::DeviceSize>::max() /
        sizeof(GpuRestirReservoir)) {
        throw std::overflow_error("ReSTIR reservoir buffer size overflow");
    }
    const auto byte_size = pixel_count * sizeof(GpuRestirReservoir);
    if (byte_size > device.physical_device()
        .getProperties().limits.maxStorageBufferRange) {
        throw std::length_error(
            "ReSTIR reservoir exceeds maxStorageBufferRange at this resolution"
        );
    }
    const BufferDesc desc{
        .size = byte_size,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memory = BufferMemoryUsage::GpuOnly
    };
    render_graph.create_buffer(std::string{current_resource}, desc);
    render_graph.create_buffer(std::string{final_resource}, desc);
}

auto RestirDiPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass is already initialized");
    }
    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    impl_->pipeline_layout = create_pipeline_layout(
        device_, impl_->descriptor_set_layout
    );
    impl_->initial_temporal_pipeline = create_pipeline(
        device_, impl_->pipeline_layout,
        "./spv/restir_di_initial_temporal.spv"
    );
    impl_->spatial_pipeline = create_pipeline(
        device_, impl_->pipeline_layout,
        "./spv/restir_di_spatial.spv"
    );

    const auto count = render_graph_.frames_in_flight_count();
    impl_->descriptor_pool = create_descriptor_pool(device_, count);
    impl_->descriptor_sets = allocate_descriptor_sets(
        device_, impl_->descriptor_pool, impl_->descriptor_set_layout, count
    );
    impl_->frame_buffers = create_upload_buffers(
        impl_->allocator, count, sizeof(GpuRestirFrame),
        vk::BufferUsageFlagBits::eUniformBuffer
    );
    impl_->light_buffers = create_upload_buffers(
        impl_->allocator, count, sizeof(GpuRestirPointLight),
        vk::BufferUsageFlagBits::eStorageBuffer
    );
    impl_->light_buffer_capacities.assign(count, 1);
    impl_->command_slots = create_command_slots(device_, count);

    for (uint32_t index = 0; index < count; ++index) {
        const GpuRestirFrame initial_frame{};
        const GpuRestirPointLight initial_light{};
        impl_->frame_buffers[index].write(&initial_frame, sizeof(initial_frame));
        impl_->light_buffers[index].write(&initial_light, sizeof(initial_light));

        const std::array buffer_infos{
            vk::DescriptorBufferInfo{
                .buffer = impl_->frame_buffers[index].get(),
                .offset = 0,
                .range = sizeof(GpuRestirFrame)
            },
            vk::DescriptorBufferInfo{
                .buffer = impl_->light_buffers[index].get(),
                .offset = 0,
                .range = impl_->light_buffers[index].size()
            },
            vk::DescriptorBufferInfo{
                .buffer = render_graph_.buffer(current_resource).get(),
                .offset = 0,
                .range = render_graph_.buffer(current_resource).size()
            },
            vk::DescriptorBufferInfo{
                .buffer = render_graph_.buffer(final_resource).get(),
                .offset = 0,
                .range = render_graph_.buffer(final_resource).size()
            }
        };
        const std::array image_infos{
            image_info(render_graph_.image(
                GeometryPass::base_color_ao_resource, index
            )),
            image_info(render_graph_.image(
                GeometryPass::normal_rm_resource, index
            )),
            image_info(render_graph_.image(
                GeometryPass::depth_resource, index
            )),
            image_info(render_graph_.image(
                GeometryPass::motion_resource, index
            ))
        };
        std::array<vk::WriteDescriptorSet, 8> writes{};
        constexpr std::array<uint32_t, 4> buffer_bindings{0, 1, 5, 6};
        constexpr std::array<uint32_t, 4> image_bindings{2, 3, 4, 7};
        for (std::size_t buffer = 0; buffer < buffer_bindings.size(); ++buffer) {
            writes[buffer_bindings[buffer]]
                .setDstSet(*impl_->descriptor_sets[index])
                .setDstBinding(buffer_bindings[buffer])
                .setDescriptorType(buffer == 0
                    ? vk::DescriptorType::eUniformBuffer
                    : vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(buffer_infos[buffer]);
        }
        for (uint32_t texture = 0; texture < gbuffer_texture_count; ++texture) {
            writes[image_bindings[texture]]
                .setDstSet(*impl_->descriptor_sets[index])
                .setDstBinding(image_bindings[texture])
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(image_infos[texture]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});
    }
    impl_->initialized = true;
}

auto RestirDiPass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), GeometryPass::pass_name);
    render_graph.set_image_usage(
        name(), GeometryPass::base_color_ao_resource, ImageUsage::ComputeSampled
    );
    render_graph.set_image_usage(
        name(), GeometryPass::normal_rm_resource, ImageUsage::ComputeSampled
    );
    render_graph.set_image_usage(
        name(), GeometryPass::depth_resource, ImageUsage::ComputeSampled
    );
    render_graph.set_image_usage(
        name(), GeometryPass::motion_resource, ImageUsage::ComputeSampled
    );
    // Both buffers are read and written within this node. The dispatch-to-
    // dispatch barrier is recorded explicitly in record().
    render_graph.set_buffer_usage(
        name(), current_resource, BufferUsage::ComputeStorageWrite
    );
    render_graph.set_buffer_usage(
        name(), final_resource, BufferUsage::ComputeStorageWrite
    );
}

auto RestirDiPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized first");
    }
    const auto& lights = scene.point_lights();
    if (lights.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("point-light count exceeds uint32_t");
    }
    const auto frame_index = render_graph_.current_frame_index();
    const auto required_capacity = std::max<std::size_t>(lights.size(), 1);
    if (required_capacity > impl_->light_buffer_capacities[frame_index]) {
        const auto size = light_buffer_size(required_capacity);
        if (size > device_.physical_device()
            .getProperties().limits.maxStorageBufferRange) {
            throw std::length_error("ReSTIR light buffer exceeds maxStorageBufferRange");
        }
        impl_->light_buffers[frame_index] = Buffer{
            impl_->allocator,
            BufferDesc{
                .size = size,
                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                .memory = BufferMemoryUsage::Upload,
                .persistent_mapping = true
            }
        };
        impl_->light_buffer_capacities[frame_index] = required_capacity;
        const vk::DescriptorBufferInfo info{
            .buffer = impl_->light_buffers[frame_index].get(),
            .offset = 0,
            .range = size
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*impl_->descriptor_sets[frame_index])
                .setDstBinding(1)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->gpu_lights.clear();
    impl_->gpu_lights.reserve(lights.size());
    for (const auto& light : lights) {
        impl_->gpu_lights.push_back(gpu_light(light));
    }
    if (!impl_->gpu_lights.empty()) {
        impl_->light_buffers[frame_index].write(
            impl_->gpu_lights.data(),
            light_buffer_size(impl_->gpu_lights.size())
        );
    }

    const bool same_lights = lights.size() == impl_->previous_lights.size() &&
        std::equal(lights.begin(), lights.end(), impl_->previous_lights.begin(),
            same_light);
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto aspect = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const auto view_projection =
        scene.camera().projection_matrix(aspect) * scene.camera().view_matrix();
    const auto inverse_view_projection = glm::inverse(view_projection);
    const GpuRestirFrame frame{
        .inverse_view_projection = inverse_view_projection,
        .view_projection = view_projection,
        .camera_position = glm::vec4{scene.camera().position(), 1.0F},
        .light_count_viewport = {
            static_cast<uint32_t>(lights.size()), extent.width, extent.height,
            impl_->history_valid && same_lights ? 1U : 0U
        },
        .sampling = {
            impl_->frame_sequence++, candidates_per_pixel,
            spatial_neighbors_per_pixel, spatial_radius_pixels
        },
        .reuse_limits = {
            max_reused_sample_count, 0U, 0U, 0U
        }
    };
    impl_->frame_buffers[frame_index].write(&frame, sizeof(frame));
    impl_->previous_lights.assign(lights.begin(), lights.end());
    impl_->history_valid = true;
}

auto RestirDiPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized first");
    }
    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->command_slots.at(frame_index);
    slot.pool.reset();
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    auto& commands = slot.command_buffer;
    commands.begin(begin_info);
    const std::array sets{*impl_->descriptor_sets.at(frame_index)};
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *impl_->pipeline_layout, 0, sets, {}
    );
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const uint32_t groups_x = (extent.width + thread_group_size - 1) /
        thread_group_size;
    const uint32_t groups_y = (extent.height + thread_group_size - 1) /
        thread_group_size;
    commands.bindPipeline(
        vk::PipelineBindPoint::eCompute,
        *impl_->initial_temporal_pipeline
    );
    commands.dispatch(groups_x, groups_y, 1);

    const auto& current = render_graph_.buffer(current_resource);
    const auto& final = render_graph_.buffer(final_resource);
    const std::array barriers{
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(current.get())
            .setOffset(0)
            .setSize(current.size()),
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(final.get())
            .setOffset(0)
            .setSize(final.size())
    };
    commands.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, barriers, {}
    );
    commands.bindPipeline(
        vk::PipelineBindPoint::eCompute,
        *impl_->spatial_pipeline
    );
    commands.dispatch(groups_x, groups_y, 1);
    commands.end();
    return *commands;
}
