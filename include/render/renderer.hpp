#ifndef RENDERER
#define RENDERER

#include "platform/window.hpp"
#include "gfx/core/context.hpp"
#include "gfx/presentation/swapchain_resources.hpp"

class Renderer {
public:
    Renderer() = default;
    
    // create the renderer
    auto create(const Window& window) -> void {
        context_.create(window);
        swapchain_resources_.create(context_, window);
    }

private:
    Context context_;
    SwapchainResources swapchain_resources_;
};

#endif
