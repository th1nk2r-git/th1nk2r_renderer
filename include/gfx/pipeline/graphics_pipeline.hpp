#ifndef GRAPHICS_PIPELINE_HPP
#define GRAPHICS_PIPELINE_HPP

#include "gfx/core/device.hpp"
#include "gfx/pipeline/graphics_pipeline_desc.hpp"

class GraphicsPipeline {
public:
    GraphicsPipeline() = default;

    GraphicsPipeline(const Device& device, const GraphicsPipelineDesc& desc);

    auto get() const -> const vk::raii::Pipeline& {
        return handle_;
    }

private:
    vk::raii::Pipeline handle_ = nullptr;
};

#endif
