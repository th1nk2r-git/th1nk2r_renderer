#include "gfx/pipeline/pipeline_layout.hpp"

PipelineLayout::PipelineLayout(const Device& device, std::span<const vk::DescriptorSetLayout> set_layouts) {
    vk::PipelineLayoutCreateInfo create_info{};
    create_info.setSetLayouts(set_layouts);
    
    handle_ = device.logical_device().createPipelineLayout(create_info);
}