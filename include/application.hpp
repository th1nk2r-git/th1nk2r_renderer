#ifndef APPLICATION
#define APPLICATION

#include "window_manager.hpp"
#include "vulkan_context.hpp"

class Application {
public:

    auto init() -> void;

    auto run() -> void;

private:
    VulkanContext vulkan_context;
    WindowManager window_manager;
};

#endif