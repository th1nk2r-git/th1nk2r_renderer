#ifndef MIPMAP_GENERATOR_HPP
#define MIPMAP_GENERATOR_HPP

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

struct MipmapGenerationDesc {
    vk::Image image{};
    vk::Extent3D base_extent{};
    vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor;
    uint32_t base_array_layer = 0;
    uint32_t layer_count = 1;
    uint32_t mip_levels = 1;
    vk::ImageLayout final_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::PipelineStageFlags destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
    vk::AccessFlags destination_access = vk::AccessFlagBits::eShaderRead;
    vk::Filter filter = vk::Filter::eLinear;
};

// Record barriers and blits that generate every mip level after level zero.
// Level zero must already be in eTransferDstOptimal with valid contents.
auto record_mipmap_generation(
    vk::raii::CommandBuffer& command_buffer,
    const MipmapGenerationDesc& desc
) -> void;

#endif
