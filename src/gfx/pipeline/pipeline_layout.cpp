#include "gfx/pipeline/pipeline_layout.hpp"

PipelineLayout::PipelineLayout(const Device& device) {
    vk::PipelineLayoutCreateInfo create_info{};

    handle_ = device.logical_device().createPipelineLayout(create_info);
}
