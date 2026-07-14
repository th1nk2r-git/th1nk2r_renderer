#ifndef RENDERER
#define RENDERER

#include "platform/window.hpp"
#include "gfx/core/device_context.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/frame/frame_context.hpp"

class Renderer {
public:
    Renderer() = default;
    
    // create the renderer
    auto create(const Window& window) -> void;

    // render a frame
    auto render() -> void;

    // wait gpu
    auto wait_idle() const -> void {
        device_context_.device().logical_device().waitIdle();
    }

    // recreate the swapchain
    auto recreate_swapchain(const Window& window) -> void;

private:
    DeviceContext device_context_;
    SwapchainContext swapchain_context_;
    FrameContext frame_context_;

    PipelineLayout main_pipeline_layout_;
    GraphicsPipeline main_pipeline_;

    // create the default graphics pipeline
    auto create_main_pipeline() -> void;

    // submit the command
    auto submit(uint32_t image_id) -> void;

    // present the image
    auto present(uint32_t image_id) -> vk::Result;
};

#endif
