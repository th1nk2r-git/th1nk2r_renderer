#include "render/pass/restir_di/restir_di_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
#include "render/pass/tlas_build/tlas_build_pass.hpp"
#include "render/render_graph.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t group_size = 8;

    struct alignas(16) GpuFrame {
        glm::mat4 inverse_view_projection{1.0F};
        glm::mat4 view_projection{1.0F};
        glm::vec4 camera_position{0.0F};
        glm::uvec4 light_count_viewport{0U};
        glm::uvec4 sampling{0U};
        glm::uvec4 reuse_limits{0U};
        glm::vec4 rejection{0.0F};
        glm::vec4 visibility{0.0F};
    };

    struct alignas(16) GpuPointLight {
        glm::vec4 position_range{0.0F};
        glm::vec4 color_intensity{0.0F};
        glm::vec4 radius{0.0F};
    };

    static_assert(sizeof(GpuFrame) == 224);
    static_assert(offsetof(GpuFrame, inverse_view_projection) == 0);
    static_assert(offsetof(GpuFrame, view_projection) == 64);
    static_assert(offsetof(GpuFrame, camera_position) == 128);
    static_assert(offsetof(GpuFrame, light_count_viewport) == 144);
    static_assert(offsetof(GpuFrame, sampling) == 160);
    static_assert(offsetof(GpuFrame, reuse_limits) == 176);
    static_assert(offsetof(GpuFrame, rejection) == 192);
    static_assert(offsetof(GpuFrame, visibility) == 208);
    static_assert(sizeof(GpuPointLight) == 48);
    static_assert(offsetof(GpuPointLight, position_range) == 0);
    static_assert(offsetof(GpuPointLight, color_intensity) == 16);
    static_assert(offsetof(GpuPointLight, radius) == 32);

    struct FrameSlot {
        Buffer frame_buffer;
        Buffer light_buffer;
        std::size_t light_capacity = 1;
        vk::raii::DescriptorSet descriptor_set = nullptr;
        vk::raii::CommandPool command_pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto validate_settings(const RestirDiPass::Settings& settings) -> void {
        if (settings.candidate_count == 0 || settings.temporal_history_length == 0) {
            throw std::invalid_argument("ReSTIR candidate count and history length must be greater than zero");
        }
        const uint64_t history_length = settings.temporal_reuse ? settings.temporal_history_length : 0;
        const uint64_t current_count = static_cast<uint64_t>(settings.candidate_count) * (history_length + 1);
        const uint64_t spatial_count = settings.spatial_reuse ? static_cast<uint64_t>(settings.spatial_neighbor_count) + 1 : 1;
        const auto max_count = std::numeric_limits<uint32_t>::max();
        if (current_count > max_count || current_count > max_count / spatial_count) {
            throw std::overflow_error("ReSTIR reservoir sample count exceeds uint32_t");
        }
        if (settings.spatial_radius > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            throw std::invalid_argument("ReSTIR spatial radius exceeds int32_t");
        }
        if (!std::isfinite(settings.depth_threshold) || settings.depth_threshold < 0.0F ||
            !std::isfinite(settings.normal_threshold) || settings.normal_threshold < -1.0F || settings.normal_threshold > 1.0F ||
            !std::isfinite(settings.roughness_threshold) || settings.roughness_threshold < 0.0F || settings.roughness_threshold > 1.0F ||
            !std::isfinite(settings.metallic_threshold) || settings.metallic_threshold < 0.0F || settings.metallic_threshold > 1.0F ||
            !std::isfinite(settings.ray_bias) || settings.ray_bias <= 0.0F) {
            throw std::invalid_argument("ReSTIR rejection thresholds or ray bias are invalid");
        }
    }

    auto validate_extent(const Device& device, vk::Extent2D extent) -> vk::DeviceSize {
        if (extent.width == 0 || extent.height == 0) {
            throw std::invalid_argument("ReSTIR reservoirs require a non-zero extent");
        }
        const auto limits = device.physical_device().getProperties().limits;
        const auto groups_x = (extent.width - 1) / group_size + 1;
        const auto groups_y = (extent.height - 1) / group_size + 1;
        if (groups_x > limits.maxComputeWorkGroupCount[0] || groups_y > limits.maxComputeWorkGroupCount[1] ||
            group_size > limits.maxComputeWorkGroupSize[0] || group_size > limits.maxComputeWorkGroupSize[1] ||
            group_size * group_size > limits.maxComputeWorkGroupInvocations) {
            throw std::length_error("ReSTIR dispatch exceeds the device limits");
        }
        const auto pixels = static_cast<vk::DeviceSize>(extent.width) * extent.height;
        if (pixels > limits.maxStorageBufferRange / RestirDiPass::reservoir_size) {
            throw std::length_error("ReSTIR reservoir exceeds maxStorageBufferRange");
        }
        return pixels * RestirDiPass::reservoir_size;
    }

    auto create_descriptor_set_layout(const Device& device) -> vk::raii::DescriptorSetLayout {
        constexpr std::array types{
            vk::DescriptorType::eUniformBuffer,
            vk::DescriptorType::eStorageBuffer,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eStorageBuffer,
            vk::DescriptorType::eStorageBuffer,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eAccelerationStructureKHR
        };
        std::array<vk::DescriptorSetLayoutBinding, types.size()> bindings{};
        for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
            bindings[binding].setBinding(binding).setDescriptorType(types[binding]).setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eCompute);
        }
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_pipeline(const Device& device, const vk::raii::PipelineLayout& layout, const std::filesystem::path& path) -> vk::raii::Pipeline {
        const auto code = load_spirv(path);
        const auto shader = device.logical_device().createShaderModule(vk::ShaderModuleCreateInfo{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()
        });
        return ComputePipelineFactory::create(device, ComputePipelineDesc{.compute_shader = &shader, .layout = &layout});
    }

    auto create_descriptor_pool(const Device& device, uint32_t frame_count) -> vk::raii::DescriptorPool {
        const std::array sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, frame_count * 3},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, frame_count * 5},
            vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, frame_count}
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet).setMaxSets(frame_count).setPoolSizes(sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto gpu_light(const PointLight& light) -> GpuPointLight {
        return {
            .position_range = glm::vec4{light.position, light.range},
            .color_intensity = glm::vec4{light.color, light.intensity},
            .radius = glm::vec4{light.radius, 0.0F, 0.0F, 0.0F}
        };
    }

    auto same_light(const GpuPointLight& first, const GpuPointLight& second) noexcept -> bool {
        return first.position_range == second.position_range && first.color_intensity == second.color_intensity && first.radius == second.radius;
    }

    auto buffer_info(const Buffer& buffer) -> vk::DescriptorBufferInfo {
        return {.buffer = buffer.get(), .offset = 0, .range = buffer.size()};
    }

    auto image_info(const Image& image) -> vk::DescriptorImageInfo {
        return {.imageView = *image.view(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    }
}

struct RestirDiPass::Impl {
    Impl(const MemoryAllocator& memory_allocator, const TlasBuildPass& tlas_pass) : allocator(memory_allocator), tlas_build_pass(tlas_pass) {}

    const MemoryAllocator& allocator;
    const TlasBuildPass& tlas_build_pass;
    Settings settings;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline initial_pipeline = nullptr;
    vk::raii::Pipeline spatial_pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<FrameSlot> slots;
    std::vector<GpuPointLight> lights;
    std::vector<GpuPointLight> next_lights;
    uint32_t sequence = 0;
    bool history_valid = false;
    bool initialized = false;
};

RestirDiPass::RestirDiPass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph, const TlasBuildPass& tlas_build_pass)
    : RenderPass(std::string{pass_name}, device, render_graph), impl_(std::make_unique<Impl>(allocator, tlas_build_pass)) {}

RestirDiPass::~RestirDiPass() = default;

auto RestirDiPass::declare_resources(const Device& device, RenderGraph& render_graph, vk::Extent2D extent) -> void {
    const BufferDesc desc{.size = validate_extent(device, extent), .memory = BufferMemoryUsage::GpuOnly};
    for (const auto resource : {current_resource, final_resource}) {
        render_graph.create_buffer(std::string{resource}, desc, ResourceMultiplicity::Single);
    }
}

auto RestirDiPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass is already initialized");
    }
    validate_settings(impl_->settings);
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    static_cast<void>(validate_extent(device_, vk::Extent2D{extent.width, extent.height}));
    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    const std::array layouts{*impl_->descriptor_set_layout};
    vk::PipelineLayoutCreateInfo layout_info{};
    layout_info.setSetLayouts(layouts);
    impl_->pipeline_layout = device_.logical_device().createPipelineLayout(layout_info);
    impl_->initial_pipeline = create_pipeline(device_, impl_->pipeline_layout, "./spv/restir_di_initial_temporal.spv");
    impl_->spatial_pipeline = create_pipeline(device_, impl_->pipeline_layout, "./spv/restir_di_spatial.spv");

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->descriptor_pool = create_descriptor_pool(device_, frame_count);
    const std::vector<vk::DescriptorSetLayout> set_layouts(frame_count, *impl_->descriptor_set_layout);
    vk::DescriptorSetAllocateInfo allocate_info{};
    allocate_info.setDescriptorPool(*impl_->descriptor_pool).setSetLayouts(set_layouts);
    auto descriptor_sets = device_.logical_device().allocateDescriptorSets(allocate_info);
    impl_->slots.reserve(frame_count);
    for (uint32_t index = 0; index < frame_count; ++index) {
        FrameSlot slot;
        slot.frame_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = sizeof(GpuFrame), .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload, .persistent_mapping = true
        }};
        slot.light_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = sizeof(GpuPointLight), .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memory = BufferMemoryUsage::Upload, .persistent_mapping = true
        }};
        const GpuFrame initial_frame{};
        const GpuPointLight initial_light{};
        slot.frame_buffer.write(&initial_frame, sizeof(initial_frame));
        slot.light_buffer.write(&initial_light, sizeof(initial_light));
        slot.descriptor_set = std::move(descriptor_sets[index]);

        const std::array buffer_infos{
            buffer_info(slot.frame_buffer), buffer_info(slot.light_buffer),
            buffer_info(render_graph_.buffer(current_resource)), buffer_info(render_graph_.buffer(final_resource))
        };
        const std::array image_infos{
            image_info(render_graph_.image(GeometryPass::base_color_ao_resource, index)),
            image_info(render_graph_.image(GeometryPass::normal_roughness_resource, index)),
            image_info(render_graph_.image(GeometryPass::emissive_metallic_resource, index)),
            image_info(render_graph_.image(GeometryPass::depth_resource, index)),
            image_info(render_graph_.image(GeometryPass::motion_resource, index))
        };
        std::array<vk::WriteDescriptorSet, 10> writes{};
        constexpr std::array<uint32_t, 4> buffer_bindings{0, 1, 6, 7};
        for (uint32_t buffer = 0; buffer < buffer_bindings.size(); ++buffer) {
            const auto binding = buffer_bindings[buffer];
            writes[binding].setDstSet(*slot.descriptor_set).setDstBinding(binding)
                .setDescriptorType(binding == 0 ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(buffer_infos[buffer]);
        }
        constexpr std::array<uint32_t, 5> image_bindings{2, 3, 4, 5, 8};
        for (uint32_t image = 0; image < image_bindings.size(); ++image) {
            const auto binding = image_bindings[image];
            writes[binding].setDstSet(*slot.descriptor_set).setDstBinding(binding)
                .setDescriptorType(vk::DescriptorType::eSampledImage).setImageInfo(image_infos[image]);
        }
        const vk::AccelerationStructureKHR tlas_handle = *impl_->tlas_build_pass.handle(index);
        vk::WriteDescriptorSetAccelerationStructureKHR tlas_info{};
        tlas_info.setAccelerationStructures(tlas_handle);
        writes[9].setPNext(&tlas_info).setDstSet(*slot.descriptor_set).setDstBinding(9)
            .setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
        device_.logical_device().updateDescriptorSets(writes, {});

        slot.command_pool = device_.logical_device().createCommandPool(vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = device_.graphics_family()
        });
        const vk::CommandBufferAllocateInfo command_info{
            .commandPool = *slot.command_pool, .level = vk::CommandBufferLevel::eSecondary, .commandBufferCount = 1
        };
        auto command_buffers = device_.logical_device().allocateCommandBuffers(command_info);
        slot.command_buffer = std::move(command_buffers.front());
        impl_->slots.push_back(std::move(slot));
    }
    impl_->initialized = true;
}

auto RestirDiPass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), TlasBuildPass::pass_name);
    render_graph.add_dependency(name(), GeometryPass::pass_name);
    for (const auto resource : {current_resource, final_resource}) {
        render_graph.set_buffer_usage(name(), resource, BufferUsage::ComputeStorageWrite);
    }
    for (const auto resource : {GeometryPass::base_color_ao_resource, GeometryPass::normal_roughness_resource,
                               GeometryPass::emissive_metallic_resource, GeometryPass::depth_resource, GeometryPass::motion_resource}) {
        render_graph.set_image_usage(name(), resource, ImageUsage::ComputeSampled);
    }
}

auto RestirDiPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized before preparation");
    }
    const auto& lights = scene.point_lights();
    if (lights.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("point-light count exceeds uint32_t");
    }
    impl_->next_lights.clear();
    impl_->next_lights.reserve(lights.size());
    for (const auto& light : lights) {
        impl_->next_lights.push_back(gpu_light(light));
    }
    if (impl_->lights.size() != impl_->next_lights.size() || !std::equal(impl_->lights.begin(), impl_->lights.end(), impl_->next_lights.begin(), same_light)) {
        reset_history();
    }
    impl_->lights.swap(impl_->next_lights);

    auto& slot = impl_->slots.at(render_graph_.current_frame_index());
    const auto required_capacity = std::max<std::size_t>(lights.size(), 1);
    if (required_capacity > slot.light_capacity) {
        const auto max_range = device_.physical_device().getProperties().limits.maxStorageBufferRange;
        if (required_capacity > max_range / sizeof(GpuPointLight)) {
            throw std::length_error("point-light buffer exceeds maxStorageBufferRange");
        }
        slot.light_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = static_cast<vk::DeviceSize>(required_capacity) * sizeof(GpuPointLight),
            .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = BufferMemoryUsage::Upload, .persistent_mapping = true
        }};
        slot.light_capacity = required_capacity;
        const auto light_info = buffer_info(slot.light_buffer);
        const std::array writes{
            vk::WriteDescriptorSet{}.setDstSet(*slot.descriptor_set).setDstBinding(1)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer).setBufferInfo(light_info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
    }
    if (!impl_->lights.empty()) {
        slot.light_buffer.write(impl_->lights.data(), static_cast<vk::DeviceSize>(impl_->lights.size()) * sizeof(GpuPointLight));
    }

    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const auto view_projection = scene.camera().projection_matrix(aspect) * scene.camera().view_matrix();
    const auto& settings = impl_->settings;
    const GpuFrame frame{
        .inverse_view_projection = glm::inverse(view_projection),
        .view_projection = view_projection,
        .camera_position = glm::vec4{scene.camera().position(), 1.0F},
        .light_count_viewport = {static_cast<uint32_t>(lights.size()), extent.width, extent.height, impl_->history_valid && settings.temporal_reuse ? 1U : 0U},
        .sampling = {impl_->sequence++, settings.candidate_count, settings.spatial_reuse ? settings.spatial_neighbor_count : 0U, settings.spatial_radius},
        .reuse_limits = {settings.temporal_reuse ? settings.candidate_count * settings.temporal_history_length : 0U, 0U, 0U, 0U},
        .rejection = {settings.depth_threshold, settings.normal_threshold, settings.roughness_threshold, settings.metallic_threshold},
        .visibility = {settings.ray_bias, 0.0F, 0.0F, 0.0F}
    };
    slot.frame_buffer.write(&frame, sizeof(frame));
    impl_->history_valid = true;
}

auto RestirDiPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized before recording");
    }
    auto& slot = impl_->slots.at(render_graph_.current_frame_index());
    slot.command_pool.reset();
    vk::CommandBufferInheritanceInfo inheritance{};
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit).setPInheritanceInfo(&inheritance);
    auto& command_buffer = slot.command_buffer;
    command_buffer.begin(begin_info);

    // The shared final reservoir is read as history before spatial reuse overwrites it.
    const std::array start_barriers{
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            .dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR
        },
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        }
    };
    const auto source_stages = vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR |
        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader;
    command_buffer.pipelineBarrier(source_stages, vk::PipelineStageFlagBits::eComputeShader, {}, start_barriers, {}, {});
    const std::array descriptor_sets{*slot.descriptor_set};
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *impl_->pipeline_layout, 0, descriptor_sets, {});
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto groups_x = (extent.width - 1) / group_size + 1;
    const auto groups_y = (extent.height - 1) / group_size + 1;
    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *impl_->initial_pipeline);
    command_buffer.dispatch(groups_x, groups_y, 1);

    const auto& current = render_graph_.buffer(current_resource);
    const auto& final = render_graph_.buffer(final_resource);
    const std::array reuse_barriers{
        vk::BufferMemoryBarrier{}.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite).setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED).setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(current.get()).setOffset(0).setSize(current.size()),
        vk::BufferMemoryBarrier{}.setSrcAccessMask(vk::AccessFlagBits::eShaderRead).setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED).setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(final.get()).setOffset(0).setSize(final.size())
    };
    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {}, reuse_barriers, {});
    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *impl_->spatial_pipeline);
    command_buffer.dispatch(groups_x, groups_y, 1);
    command_buffer.end();
    return *command_buffer;
}

auto RestirDiPass::settings() const noexcept -> const Settings& {
    return impl_->settings;
}

auto RestirDiPass::set_settings(const Settings& settings) -> void {
    validate_settings(settings);
    if (settings != impl_->settings) {
        impl_->settings = settings;
        reset_history();
    }
}

auto RestirDiPass::reset_history() noexcept -> void {
    impl_->history_valid = false;
}

auto RestirDiPass::frame_buffer(uint32_t frame_index) const -> const Buffer& {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized before querying a frame buffer");
    }
    return impl_->slots.at(frame_index).frame_buffer;
}

auto RestirDiPass::light_buffer(uint32_t frame_index) const -> const Buffer& {
    if (!impl_->initialized) {
        throw std::logic_error("ReSTIR DI pass must be initialized before querying a light buffer");
    }
    return impl_->slots.at(frame_index).light_buffer;
}
