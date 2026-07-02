#ifndef APPLICATION
#define APPLICATION

#include "window.hpp"
#include "vulkan_context.hpp"
#include "surface.hpp"
#include "swapchain.hpp"

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
    Swapchain swapchain;
};

#endif