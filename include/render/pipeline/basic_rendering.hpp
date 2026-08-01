#ifndef BASIC_RENDERING_HPP
#define BASIC_RENDERING_HPP

#include "gfx/device/device.hpp"
#include "gfx/pipeline/pipeline_state.hpp"
#include "gfx/swapchain/render_pass.hpp"

class BasicRenderingPipelineFactory {
public:
    static auto create(const Device& device, const RenderPass& render_pass) -> PipelineState;
};

#endif
