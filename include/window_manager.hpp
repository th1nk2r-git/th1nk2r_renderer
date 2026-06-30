#ifndef WINDOW_MANAGER
#define WINDOW_MANAGER

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>
#include <memory>

class WindowManager {
public:
    WindowManager() = default;

    // create the window
    auto create_window(uint16_t width, uint16_t height) -> void;

    // create the surface
    auto create_surface(const vk::raii::Instance& instance) -> void;

    // return the raw pointer of the window
    auto get_window() -> GLFWwindow* {
        return window.get();
    }

    // return the reference of the surface
    auto get_surface() -> vk::raii::SurfaceKHR& {
        return surface;
    }

    ~WindowManager();

private:
    std::unique_ptr<GLFWwindow, void(*)(GLFWwindow*)> window {nullptr, glfwDestroyWindow};
    
    vk::raii::SurfaceKHR surface = nullptr;
};

#endif