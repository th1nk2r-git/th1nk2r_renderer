#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/window.hpp"
#include "rhi/vulkan_context.hpp"
#include "rhi/surface.hpp"
#include "rhi/swapchain_context.hpp"

class Application {
public:
    Application() = default;
    
    // composition root
    auto init() -> void;

    // entry point of the application
    auto run() -> void;

private:
    VulkanContext vulkan_context;
    Window window;
    Surface surface;
    SwapchainContext swapchain_context;
};

#endif