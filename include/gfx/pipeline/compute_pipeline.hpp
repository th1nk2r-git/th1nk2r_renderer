#ifndef COMPUTE_PIPELINE_HPP
#define COMPUTE_PIPELINE_HPP

#include "gfx/device/device.hpp"

struct ComputePipelineDesc {
    const vk::raii::ShaderModule* compute_shader = nullptr;
    const vk::raii::PipelineLayout* layout = nullptr;
};

namespace ComputePipelineFactory {
    auto create(
        const Device& device,
        const ComputePipelineDesc& desc
    ) -> vk::raii::Pipeline;
}

#endif
