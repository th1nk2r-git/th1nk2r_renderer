#include "render/pass/direct_light_denoise/direct_light_denoise_pass.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "NRI.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIRayTracing.h"
#include "Extensions/NRIWrapperVK.h"
#include "NRD.h"
#include "NRDIntegration.h"
#include "NRDIntegration.hpp"
#include "gfx/device/device_context.hpp"
#include "gfx/pipeline/compute_pipeline.hpp"
#include "gfx/resource/image.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/direct_light/direct_light_pass.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/render_graph.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t group_size = 8;
    constexpr nrd::Identifier relax_id = 0;
    constexpr uint32_t diffuse_index = 0;
    constexpr uint32_t specular_index = 1;

    struct PushParams {
        std::array<uint32_t, 4> viewport{};
        std::array<float, 4> depth{};
    };

    struct FrameSlot {
        Image view_z;
        Image motion;
        Image normal_roughness;
        Image diffuse_motion;
        vk::raii::DescriptorSet descriptor_set = nullptr;
        vk::raii::CommandPool command_pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
        PushParams params{};
        bool guides_initialized = false;
    };

    auto require_format(const Device& device, vk::Format format) -> void {
        const auto features = device.physical_device().getFormatProperties(format).optimalTilingFeatures;
        const auto required = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage;
        if ((features & required) != required) {
            throw std::runtime_error("NRD guide format lacks sampled/storage image support");
        }
    }

    auto guide_image(const DeviceContext& context, vk::Format format, vk::Extent3D extent) -> Image {
        return Image{context.device(), context.allocator(), ImageDesc{
            .format = format,
            .extent = extent,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
        }};
    }

    auto create_layout(const Device& device) -> vk::raii::DescriptorSetLayout {
        std::array<vk::DescriptorSetLayoutBinding, 10> bindings{};
        for (uint32_t index = 0; index < bindings.size(); ++index) {
            bindings[index].setBinding(index).setDescriptorType(index < 4 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage)
                .setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eCompute);
        }
        vk::DescriptorSetLayoutCreateInfo info{};
        info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(info);
    }

    auto create_pipeline(const Device& device, const vk::raii::PipelineLayout& layout) -> vk::raii::Pipeline {
        const auto code = load_spirv("./spv/direct_light_denoise.spv");
        const auto shader = device.logical_device().createShaderModule(vk::ShaderModuleCreateInfo{
            .codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()
        });
        return ComputePipelineFactory::create(device, ComputePipelineDesc{.compute_shader = &shader, .layout = &layout});
    }

    auto image_info(const Image& image, vk::ImageLayout layout) -> vk::DescriptorImageInfo {
        return {.imageView = *image.view(), .imageLayout = layout};
    }

    auto guide_barrier(const Image& image, vk::AccessFlags before, vk::AccessFlags after, vk::ImageLayout old_layout) -> vk::ImageMemoryBarrier {
        vk::ImageMemoryBarrier barrier{};
        barrier.setSrcAccessMask(before).setDstAccessMask(after).setOldLayout(old_layout).setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED).setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image.get()).setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        return barrier;
    }

    auto nrd_resource(const Image& image, nri::AccessBits access, nri::Layout layout) -> nrd::Resource {
        nrd::Resource resource{};
        resource.vk.image = std::bit_cast<uint64_t>(static_cast<VkImage>(image.get()));
        resource.vk.format = static_cast<VkFormat>(image.format());
        resource.state = {access, layout, nri::StageBits::COMPUTE_SHADER};
        return resource;
    }

    auto set_matrix(float (&target)[16], const glm::mat4& matrix) -> void {
        std::copy_n(glm::value_ptr(matrix), 16, target);
    }
}

struct DirectLightDenoisePass::Impl {
    Impl(const DeviceContext& device_context, vk::Extent2D image_extent, Settings value)
        : context(device_context), extent(image_extent), settings(std::move(value)) {}

    const DeviceContext& context;
    vk::Extent2D extent;
    Settings settings;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<FrameSlot> slots;
    std::array<nrd::Integration, 2> integrations;
    glm::mat4 previous_view{1.0F};
    glm::mat4 previous_projection{1.0F};
    uint32_t frame_index = 0;
    bool initialized = false;
};

DirectLightDenoisePass::DirectLightDenoisePass(const DeviceContext& device_context, RenderGraph& render_graph,
                                               vk::Extent2D extent, Settings settings)
    : RenderPass(std::string{pass_name}, device_context.device(), render_graph),
      impl_(std::make_unique<Impl>(device_context, extent, std::move(settings))) {}

DirectLightDenoisePass::~DirectLightDenoisePass() = default;

auto DirectLightDenoisePass::configure(RenderGraph& render_graph) -> void {
    const vk::Extent3D diffuse_extent{
        impl_->extent.width / 2 + impl_->extent.width % 2,
        impl_->extent.height / 2 + impl_->extent.height % 2,
        1
    };
    const ImageDesc diffuse_output{
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = diffuse_extent,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    };
    ImageDesc specular_output = diffuse_output;
    specular_output.extent = vk::Extent3D{impl_->extent.width, impl_->extent.height, 1};
    render_graph.create_image(std::string{diffuse_resource}, diffuse_output, ResourceMultiplicity::PerFrame);
    render_graph.create_image(std::string{specular_resource}, specular_output, ResourceMultiplicity::PerFrame);
    ImageDesc guide = diffuse_output;
    guide.format = vk::Format::eR32Sfloat;
    render_graph.create_image(std::string{diffuse_view_z_resource}, guide, ResourceMultiplicity::PerFrame);
    guide.format = vk::Format::eA2B10G10R10UnormPack32;
    render_graph.create_image(std::string{diffuse_normal_roughness_resource}, guide, ResourceMultiplicity::PerFrame);

    render_graph.add_dependency(name(), DirectLightPass::pass_name);
    for (const auto resource : {GeometryPass::depth_resource, GeometryPass::motion_resource, GeometryPass::normal_roughness_resource,
                                GeometryPass::emissive_metallic_resource, DirectLightPass::diffuse_resource,
                                DirectLightPass::specular_resource}) {
        render_graph.set_image_usage(name(), resource, ImageUsage::ComputeSampled);
    }
    render_graph.set_image_usage(name(), diffuse_resource, ImageUsage::ComputeStorageWrite);
    render_graph.set_image_usage(name(), specular_resource, ImageUsage::ComputeStorageWrite);
    render_graph.set_image_usage(name(), diffuse_view_z_resource, ImageUsage::ComputeStorageWrite);
    render_graph.set_image_usage(name(), diffuse_normal_roughness_resource, ImageUsage::ComputeStorageWrite);
}

auto DirectLightDenoisePass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("direct-light denoise pass is already initialized");
    }
    if (!std::isfinite(impl_->settings.denoising_range) || impl_->settings.denoising_range <= 0.0F ||
        impl_->settings.denoising_range >= std::numeric_limits<float>::max() - 1.0F) {
        throw std::invalid_argument("NRD denoising range must be finite and positive");
    }

    const auto* library = nrd::GetLibraryDesc();
    if (library == nullptr || library->normalEncoding != nrd::NormalEncoding::R10_G10_B10_A2_UNORM ||
        library->roughnessEncoding != nrd::RoughnessEncoding::LINEAR) {
        throw std::runtime_error("NRD library encoding does not match the denoise guide shader");
    }

    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto frame_count = render_graph_.frames_in_flight_count();
    if (extent.width == 0 || extent.height == 0 || extent.width > std::numeric_limits<uint16_t>::max() ||
        extent.height > std::numeric_limits<uint16_t>::max() || frame_count == 0 || frame_count > std::numeric_limits<uint8_t>::max()) {
        throw std::length_error("NRD extent or queued frame count exceeds its limits");
    }
    const auto& limits = device_.physical_device().getProperties().limits;
    const auto groups_x = (extent.width - 1) / group_size + 1;
    const auto groups_y = (extent.height - 1) / group_size + 1;
    if (groups_x > limits.maxComputeWorkGroupCount[0] || groups_y > limits.maxComputeWorkGroupCount[1] ||
        group_size > limits.maxComputeWorkGroupSize[0] || group_size > limits.maxComputeWorkGroupSize[1] ||
        group_size * group_size > limits.maxComputeWorkGroupInvocations || sizeof(PushParams) > limits.maxPushConstantsSize) {
        throw std::length_error("NRD guide dispatch exceeds device limits");
    }
    for (const auto format : {vk::Format::eR32Sfloat, vk::Format::eR16G16B16A16Sfloat, vk::Format::eA2B10G10R10UnormPack32}) {
        require_format(device_, format);
    }

    impl_->descriptor_set_layout = create_layout(device_);
    const std::array layouts{*impl_->descriptor_set_layout};
    const vk::PushConstantRange push_range{vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushParams)};
    vk::PipelineLayoutCreateInfo layout_info{};
    layout_info.setSetLayouts(layouts).setPushConstantRanges(push_range);
    impl_->pipeline_layout = device_.logical_device().createPipelineLayout(layout_info);
    impl_->pipeline = create_pipeline(device_, impl_->pipeline_layout);

    const std::array pool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, frame_count * 4},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, frame_count * 6}
    };
    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet).setMaxSets(frame_count).setPoolSizes(pool_sizes);
    impl_->descriptor_pool = device_.logical_device().createDescriptorPool(pool_info);
    const std::vector<vk::DescriptorSetLayout> set_layouts(frame_count, *impl_->descriptor_set_layout);
    vk::DescriptorSetAllocateInfo allocate_info{};
    allocate_info.setDescriptorPool(*impl_->descriptor_pool).setSetLayouts(set_layouts);
    auto sets = device_.logical_device().allocateDescriptorSets(allocate_info);

    impl_->slots.reserve(frame_count);
    for (uint32_t index = 0; index < frame_count; ++index) {
        FrameSlot slot;
        slot.view_z = guide_image(impl_->context, vk::Format::eR32Sfloat, extent);
        slot.motion = guide_image(impl_->context, vk::Format::eR16G16B16A16Sfloat, extent);
        slot.normal_roughness = guide_image(impl_->context, vk::Format::eA2B10G10R10UnormPack32, extent);
        const auto diffuse_extent = render_graph_.image(DirectLightPass::diffuse_resource).extent();
        slot.diffuse_motion = guide_image(impl_->context, vk::Format::eR16G16B16A16Sfloat, diffuse_extent);
        slot.descriptor_set = std::move(sets[index]);

        const std::array images{
            image_info(render_graph_.image(GeometryPass::depth_resource, index), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::motion_resource, index), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::normal_roughness_resource, index), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(render_graph_.image(GeometryPass::emissive_metallic_resource, index), vk::ImageLayout::eShaderReadOnlyOptimal),
            image_info(slot.view_z, vk::ImageLayout::eGeneral),
            image_info(slot.motion, vk::ImageLayout::eGeneral),
            image_info(slot.normal_roughness, vk::ImageLayout::eGeneral),
            image_info(render_graph_.image(diffuse_view_z_resource, index), vk::ImageLayout::eGeneral),
            image_info(slot.diffuse_motion, vk::ImageLayout::eGeneral),
            image_info(render_graph_.image(diffuse_normal_roughness_resource, index), vk::ImageLayout::eGeneral)
        };
        std::array<vk::WriteDescriptorSet, 10> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            writes[binding].setDstSet(*slot.descriptor_set).setDstBinding(binding).setDescriptorCount(1)
                .setDescriptorType(binding < 4 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage)
                .setImageInfo(images[binding]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});

        slot.command_pool = device_.logical_device().createCommandPool(vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = device_.graphics_family()
        });
        const vk::CommandBufferAllocateInfo command_info{
            .commandPool = *slot.command_pool, .level = vk::CommandBufferLevel::eSecondary, .commandBufferCount = 1
        };
        auto commands = device_.logical_device().allocateCommandBuffers(command_info);
        slot.command_buffer = std::move(commands.front());
        impl_->slots.push_back(std::move(slot));
    }

    const nri::QueueFamilyVKDesc queue_family{1, nri::QueueType::GRAPHICS, device_.graphics_family()};
    nri::DeviceCreationVKDesc device_desc{};
    device_desc.vkInstance = static_cast<VkInstance>(*impl_->context.instance());
    device_desc.vkDevice = static_cast<VkDevice>(*device_.logical_device());
    device_desc.vkPhysicalDevice = static_cast<VkPhysicalDevice>(*device_.physical_device());
    device_desc.queueFamilies = &queue_family;
    device_desc.queueFamilyNum = 1;
    device_desc.minorVersion = 4;
    const auto diffuse_extent = render_graph_.image(DirectLightPass::diffuse_resource).extent();
    const std::array extents{diffuse_extent, extent};
    constexpr std::array names{"direct_light_diffuse", "direct_light_specular"};
    constexpr std::array denoisers{nrd::Denoiser::RELAX_DIFFUSE, nrd::Denoiser::RELAX_SPECULAR};
    for (uint32_t index = 0; index < impl_->integrations.size(); ++index) {
        nrd::IntegrationCreationDesc integration_desc{};
        std::memcpy(integration_desc.name, names[index], std::strlen(names[index]) + 1);
        integration_desc.resourceWidth = static_cast<uint16_t>(extents[index].width);
        integration_desc.resourceHeight = static_cast<uint16_t>(extents[index].height);
        integration_desc.queuedFrameNum = static_cast<uint8_t>(frame_count);
        integration_desc.enableWholeLifetimeDescriptorCaching = false;
        integration_desc.autoWaitForIdle = true;
        const nrd::DenoiserDesc denoiser{relax_id, denoisers[index]};
        nrd::InstanceCreationDesc instance_desc{};
        instance_desc.denoisers = &denoiser;
        instance_desc.denoisersNum = 1;
        if (impl_->integrations[index].RecreateVK(integration_desc, instance_desc, device_desc) != nrd::Result::SUCCESS) {
            throw std::runtime_error("NRD RELAX initialization failed");
        }
    }
    impl_->initialized = true;
}

auto DirectLightDenoisePass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error("direct-light denoise pass must be initialized before preparation");
    }

    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const auto view = scene.camera().view_matrix();
    const auto projection = scene.camera().projection_matrix(aspect);
    const auto previous_view = impl_->frame_index == 0 ? view : impl_->previous_view;
    const auto previous_projection = impl_->frame_index == 0 ? projection : impl_->previous_projection;

    nrd::CommonSettings common{};
    set_matrix(common.viewToClipMatrix, projection);
    set_matrix(common.viewToClipMatrixPrev, previous_projection);
    set_matrix(common.worldToViewMatrix, view);
    set_matrix(common.worldToViewMatrixPrev, previous_view);
    common.motionVectorScale[0] = 1.0F;
    common.motionVectorScale[1] = 1.0F;
    common.motionVectorScale[2] = 1.0F;
    common.resourceSize[0] = common.resourceSizePrev[0] = common.rectSize[0] = common.rectSizePrev[0] = static_cast<uint16_t>(extent.width);
    common.resourceSize[1] = common.resourceSizePrev[1] = common.rectSize[1] = common.rectSizePrev[1] = static_cast<uint16_t>(extent.height);
    common.denoisingRange = impl_->settings.denoising_range;
    common.frameIndex = impl_->frame_index;
    common.accumulationMode = impl_->frame_index == 0 ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

    nrd::CommonSettings diffuse_common = common;
    const auto diffuse_extent = render_graph_.image(DirectLightPass::diffuse_resource).extent();
    diffuse_common.resourceSize[0] = diffuse_common.resourceSizePrev[0] = diffuse_common.rectSize[0] =
        diffuse_common.rectSizePrev[0] = static_cast<uint16_t>(diffuse_extent.width);
    diffuse_common.resourceSize[1] = diffuse_common.resourceSizePrev[1] = diffuse_common.rectSize[1] =
        diffuse_common.rectSizePrev[1] = static_cast<uint16_t>(diffuse_extent.height);

    impl_->integrations[diffuse_index].NewFrame();
    impl_->integrations[specular_index].NewFrame();
    if (impl_->integrations[diffuse_index].SetCommonSettings(diffuse_common) != nrd::Result::SUCCESS ||
        impl_->integrations[diffuse_index].SetDenoiserSettings(relax_id, &impl_->settings.diffuse_relax) != nrd::Result::SUCCESS ||
        impl_->integrations[specular_index].SetCommonSettings(common) != nrd::Result::SUCCESS ||
        impl_->integrations[specular_index].SetDenoiserSettings(relax_id, &impl_->settings.specular_relax) != nrd::Result::SUCCESS) {
        throw std::runtime_error("NRD RELAX frame settings failed");
    }

    auto& slot = impl_->slots.at(render_graph_.current_frame_index());
    slot.params.viewport = {extent.width, extent.height, 0, 0};
    slot.params.depth = {projection[2][2], projection[3][2], impl_->settings.denoising_range, 0.0F};
    impl_->previous_view = view;
    impl_->previous_projection = projection;
    ++impl_->frame_index;
}

auto DirectLightDenoisePass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error("direct-light denoise pass must be initialized before recording");
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

    const auto old_layout = slot.guides_initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined;
    const auto before = slot.guides_initialized ? vk::AccessFlags{vk::AccessFlagBits::eShaderRead} : vk::AccessFlags{};
    const std::array start_barriers{
        guide_barrier(slot.view_z, before, vk::AccessFlagBits::eShaderWrite, old_layout),
        guide_barrier(slot.motion, before, vk::AccessFlagBits::eShaderWrite, old_layout),
        guide_barrier(slot.normal_roughness, before, vk::AccessFlagBits::eShaderWrite, old_layout),
        guide_barrier(slot.diffuse_motion, before, vk::AccessFlagBits::eShaderWrite, old_layout)
    };
    command.pipelineBarrier(slot.guides_initialized ? vk::PipelineStageFlagBits::eComputeShader : vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, start_barriers);

    command.bindPipeline(vk::PipelineBindPoint::eCompute, *impl_->pipeline);
    const std::array sets{*slot.descriptor_set};
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *impl_->pipeline_layout, 0, sets, {});
    command.pushConstants(*impl_->pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, vk::ArrayProxy<const PushParams>(1, &slot.params));
    const auto extent = render_graph_.image(GeometryPass::depth_resource).extent();
    command.dispatch((extent.width - 1) / group_size + 1, (extent.height - 1) / group_size + 1, 1);

    const std::array ready_barriers{
        guide_barrier(slot.view_z, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral),
        guide_barrier(slot.motion, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral),
        guide_barrier(slot.normal_roughness, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral),
        guide_barrier(slot.diffuse_motion, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral),
        guide_barrier(render_graph_.image(diffuse_view_z_resource), vk::AccessFlagBits::eShaderWrite,
                      vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral),
        guide_barrier(render_graph_.image(diffuse_normal_roughness_resource), vk::AccessFlagBits::eShaderWrite,
                      vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral)
    };
    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, ready_barriers);

    nrd::ResourceSnapshot diffuse_resources{};
    diffuse_resources.restoreInitialState = true;
    diffuse_resources.SetResource(nrd::ResourceType::IN_MV,
        nrd_resource(slot.diffuse_motion, nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    diffuse_resources.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS,
        nrd_resource(render_graph_.image(diffuse_normal_roughness_resource), nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    diffuse_resources.SetResource(nrd::ResourceType::IN_VIEWZ,
        nrd_resource(render_graph_.image(diffuse_view_z_resource), nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    diffuse_resources.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST,
        nrd_resource(render_graph_.image(DirectLightPass::diffuse_resource), nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE));
    diffuse_resources.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,
        nrd_resource(render_graph_.image(diffuse_resource), nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::Layout::SHADER_RESOURCE_STORAGE));

    nrd::ResourceSnapshot specular_resources{};
    specular_resources.restoreInitialState = true;
    specular_resources.SetResource(nrd::ResourceType::IN_MV, nrd_resource(slot.motion, nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    specular_resources.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS,
        nrd_resource(slot.normal_roughness, nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    specular_resources.SetResource(nrd::ResourceType::IN_VIEWZ,
        nrd_resource(slot.view_z, nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL));
    specular_resources.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST,
        nrd_resource(render_graph_.image(DirectLightPass::specular_resource), nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE));
    specular_resources.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST,
        nrd_resource(render_graph_.image(specular_resource), nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::Layout::SHADER_RESOURCE_STORAGE));

    const nri::CommandBufferVKDesc command_desc{static_cast<VkCommandBuffer>(*command), nri::QueueType::GRAPHICS};
    impl_->integrations[diffuse_index].DenoiseVK(&relax_id, 1, command_desc, diffuse_resources);
    impl_->integrations[specular_index].DenoiseVK(&relax_id, 1, command_desc, specular_resources);
    command.end();
    slot.guides_initialized = true;
    return *command;
}
