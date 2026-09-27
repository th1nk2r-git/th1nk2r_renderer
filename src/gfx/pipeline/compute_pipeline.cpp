#include "gfx/pipeline/compute_pipeline.hpp"

#include <stdexcept>

auto ComputePipelineFactory::create(
    const Device& device,
    const ComputePipelineDesc& desc
) -> vk::raii::Pipeline {
    if (desc.compute_shader == nullptr) {
        throw std::invalid_argument(
            "compute pipeline requires a compute shader!"
        );
    }
    if (desc.layout == nullptr) {
        throw std::invalid_argument(
            "compute pipeline requires a pipeline layout!"
        );
    }

    vk::PipelineShaderStageCreateInfo stage{};
    stage
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(**desc.compute_shader)
        .setPName("main");

    vk::ComputePipelineCreateInfo create_info{};
    create_info
        .setStage(stage)
        .setLayout(**desc.layout);

    return device.logical_device().createComputePipeline(
        nullptr,
        create_info
    );
}
