#ifndef APPLICATION
#define APPLICATION

#include "window_manager.hpp"
#include "vulkan_context.hpp"

class Application {
public:

    auto init() -> void;

    auto run() -> void;

private:
    WindowManager window_manager;
    VulkanContext vulkan_context;
};

#endif