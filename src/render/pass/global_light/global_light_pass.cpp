#include "render/pass/global_light/global_light_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/pass/tlas_build/tlas_build_pass.hpp"
#include "render/render_graph.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t shader_group_count = 5;
    constexpr uint32_t frame_binding_count = 10;

    struct alignas(16) GpuFrame {
        glm::mat4 inverse_view_projection{1.0F};
        glm::vec4 camera_position{0.0F};
        glm::uvec4 viewport_samples_bounces{0U};
        glm::uvec4 frame_light_roulette{0U};
        glm::vec4 roulette_bias_distance{0.0F};
    };

    struct alignas(16) GpuLight {
        glm::vec4 position_range{0.0F};
        glm::vec4 color_intensity{0.0F};
        glm::vec4 radius{0.0F};
    };

    static_assert(sizeof(GpuFrame) == 128);
    static_assert(offsetof(GpuFrame, camera_position) == 64);
    static_assert(offsetof(GpuFrame, viewport_samples_bounces) == 80);
    static_assert(offsetof(GpuFrame, frame_light_roulette) == 96);
    static_assert(offsetof(GpuFrame, roulette_bias_distance) == 112);
    static_assert(sizeof(GpuLight) == 48);

    struct FrameSlot {
        Buffer frame_buffer;
        Buffer light_buffer;
        std::size_t light_capacity = 1;
        vk::raii::DescriptorSet descriptor_set = nullptr;
        vk::raii::CommandPool command_pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto aligned_size(vk::DeviceSize value, vk::DeviceSize alignment) -> vk::DeviceSize {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0 || value > std::numeric_limits<vk::DeviceSize>::max() - alignment + 1) {
            throw std::runtime_error("invalid ray tracing alignment");
        }
        return (value + alignment - 1) & ~(alignment - 1);
    }

    auto ray_tracing_properties(const Device& device) -> VkPhysicalDeviceRayTracingPipelinePropertiesKHR {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 device_properties{};
        device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        device_properties.pNext = &properties;
        vkGetPhysicalDeviceProperties2(*device.physical_device(), &device_properties);
        return properties;
    }

    auto shader_module(const Device& device, const std::filesystem::path& path) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        return device.logical_device().createShaderModule(vk::ShaderModuleCreateInfo{
            .codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()
        });
    }

    auto create_pipeline(const Device& device, const vk::raii::PipelineLayout& layout) -> vk::raii::Pipeline {
        const std::array paths{
            std::filesystem::path{"./spv/global_light_raygen.spv"},
            std::filesystem::path{"./spv/global_light_miss.spv"},
            std::filesystem::path{"./spv/global_light_shadow_miss.spv"},
            std::filesystem::path{"./spv/global_light_closest_hit.spv"},
            std::filesystem::path{"./spv/global_light_any_hit.spv"}
        };
        const std::array stages{
            vk::ShaderStageFlagBits::eRaygenKHR, vk::ShaderStageFlagBits::eMissKHR,
            vk::ShaderStageFlagBits::eMissKHR, vk::ShaderStageFlagBits::eClosestHitKHR,
            vk::ShaderStageFlagBits::eAnyHitKHR
        };
        std::vector<vk::raii::ShaderModule> modules;
        std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
        modules.reserve(paths.size());
        shader_stages.reserve(paths.size());
        for (std::size_t index = 0; index < paths.size(); ++index) {
            modules.push_back(shader_module(device, paths[index]));
            shader_stages.push_back(vk::PipelineShaderStageCreateInfo{
                .stage = stages[index], .module = *modules.back(), .pName = "main"
            });
        }

        std::array<vk::RayTracingShaderGroupCreateInfoKHR, shader_group_count> groups{};
        for (auto& group : groups) {
            group.generalShader = VK_SHADER_UNUSED_KHR;
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        for (uint32_t index = 0; index < 3; ++index) {
            groups[index].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
            groups[index].generalShader = index;
        }
        groups[3].type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
        groups[3].closestHitShader = 3;
        groups[3].anyHitShader = 4;
        groups[4].type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
        groups[4].anyHitShader = 4;

        vk::RayTracingPipelineCreateInfoKHR info{};
        info.setStages(shader_stages).setGroups(groups).setMaxPipelineRayRecursionDepth(1).setLayout(*layout);
        return device.logical_device().createRayTracingPipelineKHR(nullptr, nullptr, info);
    }

    auto image_info(const Image& image, vk::ImageLayout layout) -> vk::DescriptorImageInfo {
        return {.imageView = *image.view(), .imageLayout = layout};
    }

    auto buffer_info(const Buffer& buffer) -> vk::DescriptorBufferInfo {
        return {.buffer = buffer.get(), .offset = 0, .range = buffer.size()};
    }

    auto checked_light(const PointLight& light) -> GpuLight {
        const auto finite = [](float value) { return std::isfinite(value); };
        if (!finite(light.position.x) || !finite(light.position.y) || !finite(light.position.z) ||
            !finite(light.color.x) || !finite(light.color.y) || !finite(light.color.z) ||
            !finite(light.intensity) || !finite(light.radius) || !finite(light.range) ||
            light.intensity < 0.0F || light.radius <= 0.0F || light.range < 0.0F ||
            light.color.x < 0.0F || light.color.y < 0.0F || light.color.z < 0.0F) {
            throw std::invalid_argument("global light requires finite nonnegative light values and a positive radius");
        }
        return {
            .position_range = glm::vec4{light.position, light.range},
            .color_intensity = glm::vec4{light.color, light.intensity},
            .radius = glm::vec4{light.radius, 0.0F, 0.0F, 0.0F}
        };
    }
}

struct GlobalLightPass::Impl {
    Impl(const MemoryAllocator& memory_allocator, const AssetsDB& database, const TlasBuildPass& tlas,
         vk::Extent2D image_extent, Settings value)
        : allocator(memory_allocator), assets(database), tlas_pass(tlas), extent(image_extent), settings(std::move(value)) {}

    const MemoryAllocator& allocator;
    const AssetsDB& assets;
    const TlasBuildPass& tlas_pass;
    vk::Extent2D extent;
    Settings settings;
    vk::raii::DescriptorSetLayout frame_layout = nullptr;
    vk::raii::DescriptorSetLayout scene_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    vk::raii::DescriptorSet scene_set = nullptr;
    vk::raii::Sampler sampler = nullptr;
    std::vector<FrameSlot> slots;
    Buffer sbt_buffer;
    vk::StridedDeviceAddressRegionKHR raygen_region{};
    vk::StridedDeviceAddressRegionKHR miss_region{};
    vk::StridedDeviceAddressRegionKHR hit_region{};
    std::vector<GpuLight> lights;
    uint32_t frame_index = 0;
    bool initialized = false;
};

GlobalLightPass::GlobalLightPass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph,
                                 const AssetsDB& assets, const TlasBuildPass& tlas_build_pass, vk::Extent2D extent,
                                 Settings settings)
    : RenderPass(std::string{pass_name}, device, render_graph),
      impl_(std::make_unique<Impl>(allocator, assets, tlas_build_pass, extent, std::move(settings))) {}

GlobalLightPass::~GlobalLightPass() = default;

auto GlobalLightPass::declare_resources(RenderGraph& render_graph, vk::Extent2D extent) -> void {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("global light requires a nonzero extent");
    }
    const ImageDesc image{
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    };
    render_graph.create_image(std::string{diffuse_resource}, image, ResourceMultiplicity::PerFrame);
    render_graph.create_image(std::string{specular_resource}, image, ResourceMultiplicity::PerFrame);
}

auto GlobalLightPass::configure(RenderGraph& render_graph) -> void {
    render_graph.add_dependency(name(), TlasBuildPass::pass_name);
    render_graph.add_dependency(name(), GeometryPass::pass_name);
    render_graph.set_buffer_usage(name(), TlasBuildPass::instance_resource, BufferUsage::RayTracingStorageRead);
    for (const auto resource : {GeometryPass::base_color_ao_resource, GeometryPass::normal_roughness_resource,
                                GeometryPass::emissive_metallic_resource, GeometryPass::depth_resource}) {
        render_graph.set_image_usage(name(), resource, ImageUsage::RayTracingSampled);
    }
    render_graph.set_image_usage(name(), diffuse_resource, ImageUsage::RayTracingStorageWrite);
    render_graph.set_image_usage(name(), specular_resource, ImageUsage::RayTracingStorageWrite);
}

auto GlobalLightPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("global light pass is already initialized");
    }
    const auto& settings = impl_->settings;
    if (settings.samples_per_pixel == 0 || settings.max_bounces == 0 ||
        settings.russian_roulette_start_bounce > settings.max_bounces ||
        !std::isfinite(settings.russian_roulette_min_survival) ||
        !std::isfinite(settings.russian_roulette_max_survival) ||
        !std::isfinite(settings.ray_bias) || !std::isfinite(settings.max_ray_distance) ||
        settings.russian_roulette_min_survival <= 0.0F ||
        settings.russian_roulette_max_survival > 1.0F ||
        settings.russian_roulette_min_survival > settings.russian_roulette_max_survival ||
        settings.ray_bias <= 0.0F || settings.max_ray_distance <= settings.ray_bias) {
        throw std::invalid_argument("invalid global light settings");
    }

    const auto properties = ray_tracing_properties(device_);
    const auto& limits = device_.physical_device().getProperties().limits;
    const auto pixel_count = static_cast<uint64_t>(impl_->extent.width) * impl_->extent.height;
    if (properties.maxRayRecursionDepth < 1 || pixel_count > properties.maxRayDispatchInvocationCount) {
        throw std::runtime_error("global light dispatch exceeds ray tracing limits");
    }
    if (impl_->assets.material_count() == 0 || impl_->assets.texture_count() == 0) {
        throw std::runtime_error("global light requires uploaded geometry materials and textures");
    }
    if (impl_->assets.texture_count() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("global light texture count exceeds 32-bit descriptor indices");
    }
    const auto texture_count = static_cast<uint32_t>(impl_->assets.texture_count());
    if (static_cast<uint64_t>(texture_count) + 4 > limits.maxPerStageDescriptorSampledImages ||
        static_cast<uint64_t>(texture_count) + 4 > limits.maxDescriptorSetSampledImages ||
        5 > limits.maxPerStageDescriptorStorageBuffers || 5 > limits.maxDescriptorSetStorageBuffers ||
        static_cast<uint64_t>(texture_count) + 14 > limits.maxPerStageResources ||
        impl_->assets.vertex_buffer().size() > limits.maxStorageBufferRange ||
        impl_->assets.index_buffer().size() > limits.maxStorageBufferRange ||
        impl_->assets.material_buffer().size() > limits.maxStorageBufferRange) {
        throw std::length_error("global light scene resources exceed descriptor limits");
    }

    const auto raygen = vk::ShaderStageFlagBits::eRaygenKHR;
    const auto raygen_anyhit = raygen | vk::ShaderStageFlagBits::eAnyHitKHR;
    std::array<vk::DescriptorSetLayoutBinding, frame_binding_count> frame_bindings{};
    const std::array frame_types{
        vk::DescriptorType::eAccelerationStructureKHR, vk::DescriptorType::eUniformBuffer,
        vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer,
        vk::DescriptorType::eSampledImage, vk::DescriptorType::eSampledImage,
        vk::DescriptorType::eSampledImage, vk::DescriptorType::eSampledImage,
        vk::DescriptorType::eStorageImage, vk::DescriptorType::eStorageImage
    };
    for (uint32_t index = 0; index < frame_bindings.size(); ++index) {
        frame_bindings[index] = {index, frame_types[index], 1, index == 3 ? raygen_anyhit : raygen};
    }
    vk::DescriptorSetLayoutCreateInfo frame_layout_info{};
    frame_layout_info.setBindings(frame_bindings);
    impl_->frame_layout = device_.logical_device().createDescriptorSetLayout(frame_layout_info);

    std::array<vk::DescriptorSetLayoutBinding, 5> scene_bindings{};
    for (uint32_t index = 0; index < 3; ++index) {
        scene_bindings[index] = {index, vk::DescriptorType::eStorageBuffer, 1, raygen_anyhit};
    }
    scene_bindings[3] = {3, vk::DescriptorType::eSampler, 1, raygen_anyhit};
    scene_bindings[4] = {4, vk::DescriptorType::eSampledImage, texture_count, raygen_anyhit};
    vk::DescriptorSetLayoutCreateInfo scene_layout_info{};
    scene_layout_info.setBindings(scene_bindings);
    impl_->scene_layout = device_.logical_device().createDescriptorSetLayout(scene_layout_info);

    const std::array layouts{*impl_->frame_layout, *impl_->scene_layout};
    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.setSetLayouts(layouts);
    impl_->pipeline_layout = device_.logical_device().createPipelineLayout(pipeline_layout_info);
    impl_->pipeline = create_pipeline(device_, impl_->pipeline_layout);

    const auto handle_size = static_cast<vk::DeviceSize>(properties.shaderGroupHandleSize);
    const auto stride = aligned_size(handle_size, properties.shaderGroupHandleAlignment);
    if (handle_size == 0 || stride > properties.maxShaderGroupStride) {
        throw std::runtime_error("ray tracing shader group stride is unsupported");
    }
    const auto miss_offset = aligned_size(stride, properties.shaderGroupBaseAlignment);
    const auto hit_offset = aligned_size(miss_offset + 2 * stride, properties.shaderGroupBaseAlignment);
    const auto table_size = hit_offset + 2 * stride;
    impl_->sbt_buffer = Buffer{impl_->allocator, BufferDesc{
        .size = table_size + properties.shaderGroupBaseAlignment - 1,
        .usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        .memory = BufferMemoryUsage::Upload,
        .persistent_mapping = true
    }};
    const auto address = device_.logical_device().getBufferAddress(vk::BufferDeviceAddressInfo{.buffer = impl_->sbt_buffer.get()});
    const auto base_address = aligned_size(address, properties.shaderGroupBaseAlignment);
    const auto handles = impl_->pipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, shader_group_count,
                                                                                       shader_group_count * handle_size);
    std::vector<uint8_t> table(static_cast<std::size_t>(table_size), 0);
    const std::array<vk::DeviceSize, shader_group_count> offsets{
        0, miss_offset, miss_offset + stride, hit_offset, hit_offset + stride
    };
    for (uint32_t index = 0; index < shader_group_count; ++index) {
        std::memcpy(table.data() + offsets[index], handles.data() + index * handle_size, handle_size);
    }
    impl_->sbt_buffer.write(table.data(), table_size, base_address - address);
    impl_->raygen_region = {base_address, stride, stride};
    impl_->miss_region = {base_address + miss_offset, stride, 2 * stride};
    impl_->hit_region = {base_address + hit_offset, stride, 2 * stride};

    const auto frame_count = render_graph_.frames_in_flight_count();
    const std::array pool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, frame_count},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, frame_count},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, frame_count * 2 + 3},
        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, frame_count * 4 + texture_count},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, frame_count * 2},
        vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1}
    };
    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.setMaxSets(frame_count + 1).setPoolSizes(pool_sizes);
    impl_->descriptor_pool = device_.logical_device().createDescriptorPool(pool_info);
    std::vector<vk::DescriptorSetLayout> set_layouts(frame_count, *impl_->frame_layout);
    set_layouts.push_back(*impl_->scene_layout);
    vk::DescriptorSetAllocateInfo allocation_info{};
    allocation_info.setDescriptorPool(*impl_->descriptor_pool).setSetLayouts(set_layouts);
    auto sets = device_.logical_device().allocateDescriptorSets(allocation_info);
    impl_->scene_set = std::move(sets.back());

    vk::SamplerCreateInfo sampler_info{};
    sampler_info.setMagFilter(vk::Filter::eLinear).setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear).setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat).setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMaxLod(std::numeric_limits<float>::max());
    impl_->sampler = device_.logical_device().createSampler(sampler_info);
    const std::array scene_buffers{
        buffer_info(impl_->assets.vertex_buffer()), buffer_info(impl_->assets.index_buffer()),
        buffer_info(impl_->assets.material_buffer())
    };
    const vk::DescriptorImageInfo scene_sampler{.sampler = *impl_->sampler};
    std::vector<vk::DescriptorImageInfo> textures;
    textures.reserve(texture_count);
    for (uint32_t index = 0; index < texture_count; ++index) {
        const auto& texture = impl_->assets.query(ResourceId<Texture>{index});
        textures.push_back({.imageView = *texture.image_view(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    }
    std::array<vk::WriteDescriptorSet, 5> scene_writes{};
    for (uint32_t index = 0; index < 3; ++index) {
        scene_writes[index].setDstSet(*impl_->scene_set).setDstBinding(index)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer).setBufferInfo(scene_buffers[index]);
    }
    scene_writes[3].setDstSet(*impl_->scene_set).setDstBinding(3)
        .setDescriptorType(vk::DescriptorType::eSampler).setImageInfo(scene_sampler);
    scene_writes[4].setDstSet(*impl_->scene_set).setDstBinding(4).setDescriptorCount(texture_count)
        .setDescriptorType(vk::DescriptorType::eSampledImage).setImageInfo(textures);
    device_.logical_device().updateDescriptorSets(scene_writes, {});

    impl_->slots.reserve(frame_count);
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        FrameSlot slot;
        slot.frame_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = sizeof(GpuFrame), .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload, .persistent_mapping = true
        }};
        slot.light_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = sizeof(GpuLight), .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memory = BufferMemoryUsage::Upload, .persistent_mapping = true
        }};
        slot.descriptor_set = std::move(sets[frame]);

        const VkAccelerationStructureKHR tlas = *impl_->tlas_pass.handle(frame);
        VkWriteDescriptorSetAccelerationStructureKHR acceleration_info{};
        acceleration_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        acceleration_info.accelerationStructureCount = 1;
        acceleration_info.pAccelerationStructures = &tlas;
        const std::array frame_buffers{
            buffer_info(slot.frame_buffer), buffer_info(slot.light_buffer),
            buffer_info(render_graph_.buffer(TlasBuildPass::instance_resource, frame))
        };
        const std::array frame_images{
            image_info(render_graph_.image(GeometryPass::base_color_ao_resource, frame), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::normal_roughness_resource, frame), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::emissive_metallic_resource, frame), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::depth_resource, frame), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(diffuse_resource, frame), vk::ImageLayout::eGeneral),
            image_info(render_graph_.image(specular_resource, frame), vk::ImageLayout::eGeneral)
        };
        std::array<vk::WriteDescriptorSet, frame_binding_count> writes{};
        writes[0].setDstSet(*slot.descriptor_set).setDstBinding(0).setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
            .setDescriptorCount(1).setPNext(&acceleration_info);
        for (uint32_t index = 0; index < frame_buffers.size(); ++index) {
            writes[index + 1].setDstSet(*slot.descriptor_set).setDstBinding(index + 1)
                .setDescriptorType(index == 0 ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(frame_buffers[index]);
        }
        for (uint32_t index = 0; index < frame_images.size(); ++index) {
            writes[index + 4].setDstSet(*slot.descriptor_set).setDstBinding(index + 4)
                .setDescriptorType(index < 4 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage)
                .setImageInfo(frame_images[index]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});

        slot.command_pool = device_.logical_device().createCommandPool(vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = device_.graphics_family()
        });
        auto commands = device_.logical_device().allocateCommandBuffers(vk::CommandBufferAllocateInfo{
            .commandPool = *slot.command_pool, .level = vk::CommandBufferLevel::eSecondary, .commandBufferCount = 1
        });
        slot.command_buffer = std::move(commands.front());
        impl_->slots.push_back(std::move(slot));
    }
    impl_->initialized = true;
}

auto GlobalLightPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("global light pass must be initialized before preparation");
    }
    const auto& source_lights = scene.point_lights();
    if (source_lights.size() > std::numeric_limits<uint32_t>::max() ||
        source_lights.size() > device_.physical_device().getProperties().limits.maxStorageBufferRange / sizeof(GpuLight)) {
        throw std::length_error("global light count exceeds GPU buffer limits");
    }
    impl_->lights.clear();
    impl_->lights.reserve(source_lights.size());
    for (const auto& light : source_lights) {
        impl_->lights.push_back(checked_light(light));
    }

    auto& slot = impl_->slots.at(render_graph_.current_frame_index());
    if (impl_->lights.size() > slot.light_capacity) {
        slot.light_capacity = impl_->lights.size();
        slot.light_buffer = Buffer{impl_->allocator, BufferDesc{
            .size = sizeof(GpuLight) * slot.light_capacity,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }};
        const auto info = buffer_info(slot.light_buffer);
        vk::WriteDescriptorSet write{};
        write.setDstSet(*slot.descriptor_set).setDstBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer).setBufferInfo(info);
        device_.logical_device().updateDescriptorSets(write, {});
    }
    if (!impl_->lights.empty()) {
        slot.light_buffer.write(impl_->lights.data(), sizeof(GpuLight) * impl_->lights.size());
    }

    const auto aspect = static_cast<float>(impl_->extent.width) / static_cast<float>(impl_->extent.height);
    const auto view_projection = scene.camera().projection_matrix(aspect) * scene.camera().view_matrix();
    const auto& settings = impl_->settings;
    const GpuFrame frame{
        .inverse_view_projection = glm::inverse(view_projection),
        .camera_position = glm::vec4{scene.camera().position(), 1.0F},
        .viewport_samples_bounces = {impl_->extent.width, impl_->extent.height,
                                     settings.samples_per_pixel, settings.max_bounces},
        .frame_light_roulette = {impl_->frame_index++, static_cast<uint32_t>(impl_->lights.size()),
                                 settings.russian_roulette_start_bounce, settings.cull_backfaces ? 1U : 0U},
        .roulette_bias_distance = {settings.russian_roulette_min_survival, settings.russian_roulette_max_survival,
                                   settings.ray_bias, settings.max_ray_distance}
    };
    slot.frame_buffer.write(&frame, sizeof(frame));
}

auto GlobalLightPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("global light pass must be initialized before recording");
    }
    auto& slot = impl_->slots.at(render_graph_.current_frame_index());
    slot.command_pool.reset();
    vk::CommandBufferInheritanceInfo inheritance{};
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit).setPInheritanceInfo(&inheritance);
    auto& command = slot.command_buffer;
    command.begin(begin_info);
#ifndef NDEBUG
    insert_debug_marker(command, name().data());
#endif
    command.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *impl_->pipeline);
    const std::array sets{*slot.descriptor_set, *impl_->scene_set};
    command.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *impl_->pipeline_layout, 0, sets, {});
    command.traceRaysKHR(impl_->raygen_region, impl_->miss_region, impl_->hit_region, {},
                         impl_->extent.width, impl_->extent.height, 1);
    command.end();
    return *command;
}
