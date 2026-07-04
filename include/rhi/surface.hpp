#ifndef SURFACE_HPP
#define SURFACE_HPP

#include <vulkan/vulkan_raii.hpp>
#include "platform/window.hpp"

class Surface {
public:
    Surface() = default;

    // create the surface
    auto create(const vk::raii::Instance& instance, const Window& window) -> void {
        VkSurfaceKHR raw_handle;
        if (glfwCreateWindowSurface(*instance, window.get(), nullptr, &raw_handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
        handle = vk::raii::SurfaceKHR(instance, raw_handle);
    }
    
    // return the const reference of the surface handle
    auto get() const -> const vk::raii::SurfaceKHR& {
        return handle;
    }

    // query the basic surface capabilities
    auto query_capabilities(const vk::raii::PhysicalDevice& dev) const -> vk::SurfaceCapabilitiesKHR {
        return dev.getSurfaceCapabilitiesKHR(handle);
    }

    // query the available surface formats
    auto query_formats(const vk::raii::PhysicalDevice& dev) const -> std::vector<vk::SurfaceFormatKHR> {
        return dev.getSurfaceFormatsKHR(handle);
    }

    // query the available present modes
    auto query_present_modes(const vk::raii::PhysicalDevice& dev) const -> std::vector<vk::PresentModeKHR> {
        return dev.getSurfacePresentModesKHR(handle);
    }

private:
    vk::raii::SurfaceKHR handle = nullptr;
};

#endif