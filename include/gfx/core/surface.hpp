#ifndef SURFACE_HPP
#define SURFACE_HPP

#include "gfx/core/instance.hpp"
#include "platform/window.hpp"

class Surface {
public:
    Surface() = default;

    // create the surface
    auto create(const Instance& instance, const Window& window) -> void;
    
    // return the const reference of the surface handle
    auto get() const -> const vk::raii::SurfaceKHR& {
        return handle_;
    }

    // query the basic surface capabilities
    auto query_capabilities(const vk::raii::PhysicalDevice& dev) const -> vk::SurfaceCapabilitiesKHR {
        return dev.getSurfaceCapabilitiesKHR(handle_);
    }

    // query the available surface formats
    auto query_formats(const vk::raii::PhysicalDevice& dev) const -> std::vector<vk::SurfaceFormatKHR> {
        return dev.getSurfaceFormatsKHR(handle_);
    }

    // query the available present modes
    auto query_present_modes(const vk::raii::PhysicalDevice& dev) const -> std::vector<vk::PresentModeKHR> {
        return dev.getSurfacePresentModesKHR(handle_);
    }

private:
    vk::raii::SurfaceKHR handle_ = nullptr;
};

#endif
