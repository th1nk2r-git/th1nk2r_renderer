#ifndef BASIC_RENDERING_HPP
#define BASIC_RENDERING_HPP

#include "gfx/device/device.hpp"
#include "gfx/pipeline/pipeline_state.hpp"
#include "gfx/swapchain/render_pass.hpp"

class BasicRenderingPipelineFactory {
public:
    static auto create(const Device& device, const RenderPass& render_pass) -> PipelineState;

    static auto create_graphics_pipeline(
        const Device& device,
        const RenderPass& render_pass,
        const PipelineLayout& pipeline_layout
    ) -> Pipeline;

private:
    static auto create_material_descriptor_set_layout(
        const Device& device
    ) -> DescriptorSetLayout;
};

#endif
