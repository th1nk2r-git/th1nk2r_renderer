#include "gfx/pipeline/pipeline_layout.hpp"

namespace {
    auto create_pipeline_layout(
        const Device& device,
        std::span<const vk::DescriptorSetLayout> set_layouts) -> vk::raii::PipelineLayout {
        vk::PipelineLayoutCreateInfo create_info{};
        create_info.setSetLayouts(set_layouts);
        return device.logical_device().createPipelineLayout(create_info);
    }
}

PipelineLayout::PipelineLayout(
    const Device& device,
    std::span<const vk::DescriptorSetLayout> set_layouts)
    : handle_(create_pipeline_layout(device, set_layouts)) {}
