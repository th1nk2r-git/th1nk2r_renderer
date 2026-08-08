#include "gfx/device/surface.hpp"

#include <stdexcept>

namespace {
    auto create_surface(const Instance& instance, const Window& window) -> vk::raii::SurfaceKHR {
        VkSurfaceKHR raw_handle = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*instance.get(), window.get(), nullptr, &raw_handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
        return vk::raii::SurfaceKHR(instance.get(), raw_handle);
    }
}

Surface::Surface(const Instance& instance, const Window& window)
    : handle_(create_surface(instance, window)) {}
