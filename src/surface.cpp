#include "surface.hpp"

auto Surface::create(const vk::raii::Instance& instance, const Window& window) -> void {
    VkSurfaceKHR raw_handle;
    if (glfwCreateWindowSurface(*instance, window.get(), nullptr, &raw_handle) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    handle = vk::raii::SurfaceKHR(instance, raw_handle);
}