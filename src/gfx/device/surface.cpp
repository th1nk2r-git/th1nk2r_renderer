#include "gfx/device/surface.hpp"

#include <stdexcept>

Surface::Surface(const Instance& instance, const Window& window) {
    VkSurfaceKHR raw_handle;
    if (glfwCreateWindowSurface(*instance.get(), window.get(), nullptr, &raw_handle) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    handle_ = vk::raii::SurfaceKHR(instance.get(), raw_handle);
}
