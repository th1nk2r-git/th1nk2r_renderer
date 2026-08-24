#include "render/pass/forward/ibl_writer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "io/spirv_loader.hpp"

namespace {
    constexpr vk::Format ibl_format =
        vk::Format::eR32G32B32A32Sfloat;
    constexpr uint32_t cube_face_count = 6;
    constexpr uint32_t environment_resolution = 512;
    constexpr uint32_t irradiance_resolution = 32;
    constexpr uint32_t prefiltered_resolution = 128;
    constexpr uint32_t brdf_lut_resolution = 256;
    constexpr uint32_t integration_sample_count = 256;
    constexpr uint32_t workgroup_size = 8;
    constexpr uint32_t environment_mip_count =
        std::bit_width(environment_resolution);
    constexpr uint32_t prefiltered_mip_count =
        std::bit_width(prefiltered_resolution);

    struct IblConstants {
        uint32_t output_resolution = 0;
        uint32_t environment_resolution = 0;
        uint32_t sample_count = 0;
        float roughness = 0.0F;
    };

    static_assert(sizeof(IblConstants) == 16);

    auto checked_group_count(uint32_t resolution) -> uint32_t {
        return (resolution + workgroup_size - 1) / workgroup_size;
    }

    auto mip_dimension(uint32_t resolution, uint32_t mip_level) -> uint32_t {
        return std::max(resolution >> mip_level, 1U);
    }

    auto image_range(
        uint32_t base_mip_level,
        uint32_t mip_level_count,
        uint32_t base_array_layer,
        uint32_t array_layer_count
    ) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor,
            base_mip_level,
            mip_level_count,
            base_array_layer,
            array_layer_count
        };
    }

    auto validate_panorama(const HdrImageData& panorama) -> void {
        if (panorama.width == 0 || panorama.height == 0) {
            throw std::invalid_argument(
                "IBL panorama dimensions must be greater than zero"
            );
        }
        if (panorama.channels != 4) {
            throw std::invalid_argument(
                "IBL panorama must contain RGBA32F pixels"
            );
        }
        constexpr auto max_size = std::numeric_limits<size_t>::max();
        const auto width = static_cast<size_t>(panorama.width);
        const auto height = static_cast<size_t>(panorama.height);
        if (width > max_size / height ||
            width * height > max_size / panorama.channels) {
            throw std::length_error("IBL panorama size overflow");
        }
        const auto expected_size =
            width * height * panorama.channels;
        if (panorama.pixels.size() != expected_size) {
            throw std::invalid_argument(
                "IBL panorama pixel count does not match its dimensions"
            );
        }
    }

    auto require_format_features(
        const Device& device,
        vk::Format format,
        vk::FormatFeatureFlags required,
        const char* role
    ) -> void {
        const auto available = device.physical_device()
            .getFormatProperties(format)
            .optimalTilingFeatures;
        if ((available & required) != required) {
            throw std::runtime_error(
                std::string{"GPU format support is insufficient for "} +
                role
            );
        }
    }

    auto validate_device_support(
        const Device& device,
        const HdrImageData& panorama
    ) -> void {
        const auto limits = device.physical_device().getProperties().limits;
        if (panorama.width > limits.maxImageDimension2D ||
            panorama.height > limits.maxImageDimension2D) {
            throw std::out_of_range(
                "IBL panorama dimensions exceed the device limit"
            );
        }
        if (environment_resolution > limits.maxImageDimensionCube ||
            prefiltered_resolution > limits.maxImageDimensionCube ||
            irradiance_resolution > limits.maxImageDimensionCube) {
            throw std::out_of_range(
                "IBL cubemap dimensions exceed the device limit"
            );
        }

        require_format_features(
            device,
            ibl_format,
            vk::FormatFeatureFlagBits::eSampledImage |
                vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                vk::FormatFeatureFlagBits::eTransferDst,
            "the HDR panorama"
        );
        require_format_features(
            device,
            ibl_format,
            vk::FormatFeatureFlagBits::eSampledImage |
                vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                vk::FormatFeatureFlagBits::eStorageImage |
                vk::FormatFeatureFlagBits::eBlitSrc |
                vk::FormatFeatureFlagBits::eBlitDst,
            "IBL precomputation"
        );
    }

    auto create_panorama_image(
        const MemoryAllocator& allocator,
        const HdrImageData& panorama
    ) -> Image {
        return Image{
            allocator,
            ImageDesc{
                .format = ibl_format,
                .extent = vk::Extent3D{
                    panorama.width,
                    panorama.height,
                    1
                },
                .mip_levels = 1,
                .array_layers = 1,
                .usage = vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled
            }
        };
    }

    auto create_cube_image(
        const MemoryAllocator& allocator,
        uint32_t resolution,
        uint32_t mip_levels,
        vk::ImageUsageFlags usage
    ) -> Image {
        return Image{
            allocator,
            ImageDesc{
                .flags = vk::ImageCreateFlagBits::eCubeCompatible,
                .format = ibl_format,
                .extent = vk::Extent3D{resolution, resolution, 1},
                .mip_levels = mip_levels,
                .array_layers = cube_face_count,
                .usage = usage
            }
        };
    }

    auto create_brdf_lut_image(
        const MemoryAllocator& allocator
    ) -> Image {
        return Image{
            allocator,
            ImageDesc{
                .format = ibl_format,
                .extent = vk::Extent3D{
                    brdf_lut_resolution,
                    brdf_lut_resolution,
                    1
                },
                .mip_levels = 1,
                .array_layers = 1,
                .usage = vk::ImageUsageFlagBits::eStorage |
                    vk::ImageUsageFlagBits::eSampled
            }
        };
    }

    auto create_image_view(
        const Device& device,
        const Image& image,
        vk::ImageViewType view_type,
        uint32_t base_mip_level,
        uint32_t mip_level_count,
        uint32_t base_array_layer,
        uint32_t array_layer_count
    ) -> vk::raii::ImageView {
        vk::ImageViewCreateInfo create_info{};
        create_info
            .setImage(image.get())
            .setViewType(view_type)
            .setFormat(image.format())
            .setSubresourceRange(
                image_range(
                    base_mip_level,
                    mip_level_count,
                    base_array_layer,
                    array_layer_count
                )
            );
        return device.logical_device().createImageView(create_info);
    }

    auto create_sampler(
        const Device& device,
        vk::SamplerAddressMode address_u,
        vk::SamplerAddressMode address_v,
        float max_lod
    ) -> vk::raii::Sampler {
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(address_u)
            .setAddressModeV(address_v)
            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
            .setMinLod(0.0F)
            .setMaxLod(max_lod)
            .setAnisotropyEnable(false)
            .setCompareEnable(false)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }

    auto create_descriptor_set_layout(
        const Device& device,
        std::span<const vk::DescriptorSetLayoutBinding> bindings
    ) -> vk::raii::DescriptorSetLayout {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_sampled_storage_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{}
                .setBinding(0)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding{}
                .setBinding(1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding{}
                .setBinding(2)
                .setDescriptorType(vk::DescriptorType::eStorageImage)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        };
        return create_descriptor_set_layout(device, bindings);
    }

    auto create_storage_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{}
                .setBinding(0)
                .setDescriptorType(vk::DescriptorType::eStorageImage)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        };
        return create_descriptor_set_layout(device, bindings);
    }

    auto create_compute_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout
    ) -> vk::raii::PipelineLayout {
        const std::array layouts{*descriptor_set_layout};
        const std::array ranges{
            vk::PushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eCompute,
                .offset = 0,
                .size = sizeof(IblConstants)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(layouts)
            .setPushConstantRanges(ranges);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path
    ) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        return device.logical_device().createShaderModule(
            vk::ShaderModuleCreateInfo{
                .codeSize = code.size() * sizeof(uint32_t),
                .pCode = code.data()
            }
        );
    }

    auto create_compute_pipeline(
        const Device& device,
        const vk::raii::ShaderModule& shader,
        const vk::raii::PipelineLayout& layout
    ) -> vk::raii::Pipeline {
        vk::PipelineShaderStageCreateInfo stage{};
        stage
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(*shader)
            .setPName("main");
        vk::ComputePipelineCreateInfo create_info{};
        create_info.setStage(stage).setLayout(*layout);
        return device.logical_device().createComputePipeline(
            nullptr,
            create_info
        );
    }

    auto create_compute_descriptor_pool(
        const Device& device
    ) -> vk::raii::DescriptorPool {
        constexpr uint32_t sampled_storage_set_count =
            2 + prefiltered_mip_count;
        constexpr uint32_t total_set_count =
            sampled_storage_set_count + 1;
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                sampled_storage_set_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                sampled_storage_set_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eStorageImage,
                total_set_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(total_set_count)
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

    auto write_sampled_storage_set(
        const Device& device,
        const vk::raii::DescriptorSet& descriptor_set,
        const vk::raii::Sampler& sampler,
        const vk::raii::ImageView& sampled_view,
        const vk::raii::ImageView& storage_view
    ) -> void {
        const vk::DescriptorImageInfo sampler_info{.sampler = *sampler};
        const vk::DescriptorImageInfo sampled_info{
            .imageView = *sampled_view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
        const vk::DescriptorImageInfo storage_info{
            .imageView = *storage_view,
            .imageLayout = vk::ImageLayout::eGeneral
        };
        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0]
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info);
        writes[1]
            .setDstSet(*descriptor_set)
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setImageInfo(sampled_info);
        writes[2]
            .setDstSet(*descriptor_set)
            .setDstBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setImageInfo(storage_info);
        device.logical_device().updateDescriptorSets(writes, {});
    }

    auto write_storage_set(
        const Device& device,
        const vk::raii::DescriptorSet& descriptor_set,
        const vk::raii::ImageView& storage_view
    ) -> void {
        const vk::DescriptorImageInfo storage_info{
            .imageView = *storage_view,
            .imageLayout = vk::ImageLayout::eGeneral
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*descriptor_set)
                .setDstBinding(0)
                .setDescriptorType(vk::DescriptorType::eStorageImage)
                .setImageInfo(storage_info)
        };
        device.logical_device().updateDescriptorSets(writes, {});
    }

    auto transition_image(
        vk::raii::CommandBuffer& command_buffer,
        vk::Image image,
        const vk::ImageSubresourceRange& range,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags source_access,
        vk::AccessFlags destination_access,
        vk::PipelineStageFlags source_stage,
        vk::PipelineStageFlags destination_stage
    ) -> void {
        vk::ImageMemoryBarrier barrier{};
        barrier
            .setSrcAccessMask(source_access)
            .setDstAccessMask(destination_access)
            .setOldLayout(old_layout)
            .setNewLayout(new_layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(range);
        command_buffer.pipelineBarrier(
            source_stage,
            destination_stage,
            {},
            {},
            {},
            barrier
        );
    }

    auto bind_compute(
        vk::raii::CommandBuffer& command_buffer,
        const vk::raii::Pipeline& pipeline,
        const vk::raii::PipelineLayout& layout,
        const vk::raii::DescriptorSet& descriptor_set,
        const IblConstants& constants,
        uint32_t z_group_count
    ) -> void {
        command_buffer.bindPipeline(
            vk::PipelineBindPoint::eCompute,
            *pipeline
        );
        const std::array descriptor_sets{*descriptor_set};
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            *layout,
            0,
            descriptor_sets,
            {}
        );
        command_buffer.pushConstants<IblConstants>(
            *layout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            constants
        );
        const auto group_count =
            checked_group_count(constants.output_resolution);
        command_buffer.dispatch(group_count, group_count, z_group_count);
    }

    auto create_fragment_descriptor_pool(
        const Device& device
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 3}
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(1)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto write_fragment_set(
        const Device& device,
        const vk::raii::DescriptorSet& descriptor_set,
        const vk::raii::Sampler& sampler,
        const vk::raii::ImageView& irradiance_view,
        const vk::raii::ImageView& prefiltered_view,
        const vk::raii::ImageView& brdf_lut_view
    ) -> void {
        const vk::DescriptorImageInfo sampler_info{.sampler = *sampler};
        const std::array image_infos{
            vk::DescriptorImageInfo{
                .imageView = *irradiance_view,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            },
            vk::DescriptorImageInfo{
                .imageView = *prefiltered_view,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            },
            vk::DescriptorImageInfo{
                .imageView = *brdf_lut_view,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            }
        };
        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0]
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info);
        for (uint32_t index = 0; index < image_infos.size(); ++index) {
            writes[index + 1]
                .setDstSet(*descriptor_set)
                .setDstBinding(index + 1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(image_infos[index]);
        }
        device.logical_device().updateDescriptorSets(writes, {});
    }
}

IblWriter::IblWriter(
    const Device& device,
    const MemoryAllocator& allocator,
    const vk::raii::DescriptorSetLayout& descriptor_set_layout
) : device_(device),
    allocator_(allocator),
    descriptor_set_layout_(descriptor_set_layout) {}

auto IblWriter::write(
    const HdrImageData& panorama,
    ImageUploader& uploader
) -> void {
    if (ready_) {
        throw std::logic_error("IBL resources are already initialized");
    }
    validate_panorama(panorama);
    validate_device_support(device_, panorama);

    auto panorama_image = create_panorama_image(allocator_, panorama);
    auto panorama_view = create_image_view(
        device_,
        panorama_image,
        vk::ImageViewType::e2D,
        0,
        1,
        0,
        1
    );
    const auto panorama_bytes = std::as_bytes(
        std::span<const float>{panorama.pixels}
    );
    uploader.enqueue(
        panorama_bytes,
        panorama_image,
        ImageUploadDesc{
            .extent = panorama_image.extent(),
            .final_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .destination_stage = vk::PipelineStageFlagBits::eComputeShader,
            .destination_access = vk::AccessFlagBits::eShaderRead
        }
    );
    uploader.submit_and_wait();

    environment_image_ = create_cube_image(
        allocator_,
        environment_resolution,
        environment_mip_count,
        vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst
    );
    irradiance_image_ = create_cube_image(
        allocator_,
        irradiance_resolution,
        1,
        vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled
    );
    prefiltered_image_ = create_cube_image(
        allocator_,
        prefiltered_resolution,
        prefiltered_mip_count,
        vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled
    );
    brdf_lut_image_ = create_brdf_lut_image(allocator_);

    environment_view_ = create_image_view(
        device_,
        environment_image_,
        vk::ImageViewType::eCube,
        0,
        environment_mip_count,
        0,
        cube_face_count
    );
    irradiance_view_ = create_image_view(
        device_,
        irradiance_image_,
        vk::ImageViewType::eCube,
        0,
        1,
        0,
        cube_face_count
    );
    prefiltered_view_ = create_image_view(
        device_,
        prefiltered_image_,
        vk::ImageViewType::eCube,
        0,
        prefiltered_mip_count,
        0,
        cube_face_count
    );
    brdf_lut_view_ = create_image_view(
        device_,
        brdf_lut_image_,
        vk::ImageViewType::e2D,
        0,
        1,
        0,
        1
    );

    auto environment_storage_view = create_image_view(
        device_,
        environment_image_,
        vk::ImageViewType::e2DArray,
        0,
        1,
        0,
        cube_face_count
    );
    auto irradiance_storage_view = create_image_view(
        device_,
        irradiance_image_,
        vk::ImageViewType::e2DArray,
        0,
        1,
        0,
        cube_face_count
    );
    std::vector<vk::raii::ImageView> prefiltered_storage_views;
    prefiltered_storage_views.reserve(prefiltered_mip_count);
    for (uint32_t mip_level = 0;
         mip_level < prefiltered_mip_count;
         ++mip_level) {
        prefiltered_storage_views.push_back(
            create_image_view(
                device_,
                prefiltered_image_,
                vk::ImageViewType::e2DArray,
                mip_level,
                1,
                0,
                cube_face_count
            )
        );
    }
    auto brdf_lut_storage_view = create_image_view(
        device_,
        brdf_lut_image_,
        vk::ImageViewType::e2D,
        0,
        1,
        0,
        1
    );

    auto panorama_sampler = create_sampler(
        device_,
        vk::SamplerAddressMode::eRepeat,
        vk::SamplerAddressMode::eClampToEdge,
        0.0F
    );
    auto environment_sampler = create_sampler(
        device_,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        static_cast<float>(environment_mip_count - 1)
    );

    auto sampled_storage_layout = create_sampled_storage_layout(device_);
    auto storage_layout = create_storage_layout(device_);
    auto sampled_pipeline_layout = create_compute_pipeline_layout(
        device_,
        sampled_storage_layout
    );
    auto storage_pipeline_layout = create_compute_pipeline_layout(
        device_,
        storage_layout
    );

    auto equirect_shader = create_shader_module(
        device_,
        "./spv/equirect_to_cube.spv"
    );
    auto irradiance_shader = create_shader_module(
        device_,
        "./spv/irradiance.spv"
    );
    auto prefilter_shader = create_shader_module(
        device_,
        "./spv/prefilter_environment.spv"
    );
    auto brdf_shader = create_shader_module(
        device_,
        "./spv/brdf_lut.spv"
    );
    auto equirect_pipeline = create_compute_pipeline(
        device_,
        equirect_shader,
        sampled_pipeline_layout
    );
    auto irradiance_pipeline = create_compute_pipeline(
        device_,
        irradiance_shader,
        sampled_pipeline_layout
    );
    auto prefilter_pipeline = create_compute_pipeline(
        device_,
        prefilter_shader,
        sampled_pipeline_layout
    );
    auto brdf_pipeline = create_compute_pipeline(
        device_,
        brdf_shader,
        storage_pipeline_layout
    );

    auto compute_descriptor_pool = create_compute_descriptor_pool(device_);
    auto sampled_storage_sets = allocate_descriptor_sets(
        device_,
        compute_descriptor_pool,
        sampled_storage_layout,
        2 + prefiltered_mip_count
    );
    auto storage_sets = allocate_descriptor_sets(
        device_,
        compute_descriptor_pool,
        storage_layout,
        1
    );
    auto& equirect_set = sampled_storage_sets[0];
    auto& irradiance_set = sampled_storage_sets[1];
    write_sampled_storage_set(
        device_,
        equirect_set,
        panorama_sampler,
        panorama_view,
        environment_storage_view
    );
    write_sampled_storage_set(
        device_,
        irradiance_set,
        environment_sampler,
        environment_view_,
        irradiance_storage_view
    );
    for (uint32_t mip_level = 0;
         mip_level < prefiltered_mip_count;
         ++mip_level) {
        write_sampled_storage_set(
            device_,
            sampled_storage_sets[mip_level + 2],
            environment_sampler,
            environment_view_,
            prefiltered_storage_views[mip_level]
        );
    }
    write_storage_set(
        device_,
        storage_sets.front(),
        brdf_lut_storage_view
    );

    const vk::CommandPoolCreateInfo command_pool_info{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = device_.graphics_family()
    };
    auto command_pool = device_.logical_device().createCommandPool(
        command_pool_info
    );
    auto command_buffers = device_.logical_device().allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        }
    );
    auto& command_buffer = command_buffers.front();
    command_buffer.begin(
        vk::CommandBufferBeginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        }
    );

    transition_image(
        command_buffer,
        environment_image_.get(),
        image_range(0, 1, 0, cube_face_count),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        {},
        vk::AccessFlagBits::eShaderWrite,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader
    );
    bind_compute(
        command_buffer,
        equirect_pipeline,
        sampled_pipeline_layout,
        equirect_set,
        IblConstants{
            .output_resolution = environment_resolution,
            .environment_resolution = environment_resolution,
            .sample_count = integration_sample_count
        },
        cube_face_count
    );

    transition_image(
        command_buffer,
        environment_image_.get(),
        image_range(0, 1, 0, cube_face_count),
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eTransferSrcOptimal,
        vk::AccessFlagBits::eShaderWrite,
        vk::AccessFlagBits::eTransferRead,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer
    );
    for (uint32_t mip_level = 1;
         mip_level < environment_mip_count;
         ++mip_level) {
        transition_image(
            command_buffer,
            environment_image_.get(),
            image_range(mip_level, 1, 0, cube_face_count),
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            {},
            vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer
        );

        vk::ImageBlit blit{};
        blit.srcSubresource = vk::ImageSubresourceLayers{
            vk::ImageAspectFlagBits::eColor,
            mip_level - 1,
            0,
            cube_face_count
        };
        blit.srcOffsets[1] = vk::Offset3D{
            static_cast<int32_t>(mip_dimension(
                environment_resolution,
                mip_level - 1
            )),
            static_cast<int32_t>(mip_dimension(
                environment_resolution,
                mip_level - 1
            )),
            1
        };
        blit.dstSubresource = vk::ImageSubresourceLayers{
            vk::ImageAspectFlagBits::eColor,
            mip_level,
            0,
            cube_face_count
        };
        blit.dstOffsets[1] = vk::Offset3D{
            static_cast<int32_t>(mip_dimension(
                environment_resolution,
                mip_level
            )),
            static_cast<int32_t>(mip_dimension(
                environment_resolution,
                mip_level
            )),
            1
        };
        command_buffer.blitImage(
            environment_image_.get(),
            vk::ImageLayout::eTransferSrcOptimal,
            environment_image_.get(),
            vk::ImageLayout::eTransferDstOptimal,
            blit,
            vk::Filter::eLinear
        );
        transition_image(
            command_buffer,
            environment_image_.get(),
            image_range(mip_level, 1, 0, cube_face_count),
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eTransferRead,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer
        );
    }
    transition_image(
        command_buffer,
        environment_image_.get(),
        image_range(0, environment_mip_count, 0, cube_face_count),
        vk::ImageLayout::eTransferSrcOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits::eTransferRead,
        vk::AccessFlagBits::eShaderRead,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader
    );

    transition_image(
        command_buffer,
        irradiance_image_.get(),
        image_range(0, 1, 0, cube_face_count),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        {},
        vk::AccessFlagBits::eShaderWrite,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader
    );
    transition_image(
        command_buffer,
        prefiltered_image_.get(),
        image_range(0, prefiltered_mip_count, 0, cube_face_count),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        {},
        vk::AccessFlagBits::eShaderWrite,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader
    );
    transition_image(
        command_buffer,
        brdf_lut_image_.get(),
        image_range(0, 1, 0, 1),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        {},
        vk::AccessFlagBits::eShaderWrite,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader
    );

    bind_compute(
        command_buffer,
        irradiance_pipeline,
        sampled_pipeline_layout,
        irradiance_set,
        IblConstants{
            .output_resolution = irradiance_resolution,
            .environment_resolution = environment_resolution,
            .sample_count = integration_sample_count
        },
        cube_face_count
    );
    for (uint32_t mip_level = 0;
         mip_level < prefiltered_mip_count;
         ++mip_level) {
        bind_compute(
            command_buffer,
            prefilter_pipeline,
            sampled_pipeline_layout,
            sampled_storage_sets[mip_level + 2],
            IblConstants{
                .output_resolution = mip_dimension(
                    prefiltered_resolution,
                    mip_level
                ),
                .environment_resolution = environment_resolution,
                .sample_count = integration_sample_count,
                .roughness = static_cast<float>(mip_level) /
                    static_cast<float>(prefiltered_mip_count - 1)
            },
            cube_face_count
        );
    }
    bind_compute(
        command_buffer,
        brdf_pipeline,
        storage_pipeline_layout,
        storage_sets.front(),
        IblConstants{
            .output_resolution = brdf_lut_resolution,
            .sample_count = integration_sample_count
        },
        1
    );

    std::array<vk::ImageMemoryBarrier, 3> final_barriers{};
    final_barriers[0]
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(irradiance_image_.get())
        .setSubresourceRange(image_range(0, 1, 0, cube_face_count));
    final_barriers[1]
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(prefiltered_image_.get())
        .setSubresourceRange(
            image_range(
                0,
                prefiltered_mip_count,
                0,
                cube_face_count
            )
        );
    final_barriers[2]
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(brdf_lut_image_.get())
        .setSubresourceRange(image_range(0, 1, 0, 1));
    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        final_barriers
    );
    command_buffer.end();

    const vk::CommandBuffer command_buffer_handle = *command_buffer;
    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(command_buffer_handle);
    auto fence = device_.logical_device().createFence(vk::FenceCreateInfo{});
    device_.graphics_queue().submit(submit_info, fence);
    static_cast<void>(device_.logical_device().waitForFences(
        *fence,
        true,
        std::numeric_limits<uint64_t>::max()
    ));

    auto sampler = create_sampler(
        device_,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        static_cast<float>(prefiltered_mip_count - 1)
    );
    auto descriptor_pool = create_fragment_descriptor_pool(device_);
    auto descriptor_sets = allocate_descriptor_sets(
        device_,
        descriptor_pool,
        descriptor_set_layout_,
        1
    );
    auto descriptor_set = std::move(descriptor_sets.front());
    write_fragment_set(
        device_,
        descriptor_set,
        sampler,
        irradiance_view_,
        prefiltered_view_,
        brdf_lut_view_
    );

    sampler_ = std::move(sampler);
    descriptor_pool_ = std::move(descriptor_pool);
    descriptor_set_ = std::move(descriptor_set);
    ready_ = true;
}

auto IblWriter::descriptor_set() const
    -> const vk::raii::DescriptorSet& {
    if (!ready_) {
        throw std::logic_error("IBL resources are not initialized");
    }
    return descriptor_set_;
}
