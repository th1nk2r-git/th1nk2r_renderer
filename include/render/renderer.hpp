#ifndef RENDERER
#define RENDERER

#include "platform/window.hpp"
#include "gfx/core/device_context.hpp"
#include "gfx/swapchain/swapchain_context.hpp"

class Renderer {
public:
    Renderer() = default;
    
    // create the renderer
    auto create(const Window& window) -> void {
        device_context_.create(window);
        swapchain_context_.create(device_context_, window);
    }

private:
    DeviceContext device_context_;
    SwapchainContext swapchain_context_;
};

#endif
