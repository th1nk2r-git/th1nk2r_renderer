#ifndef SWAPCHAIN_HPP
#define SWAPCHAIN_HPP

#include "vulkan_context.hpp"
#include "surface.hpp"
#include "image_view.hpp"

class Swapchain {
public:
    Swapchain() = default;

    // create the swapchain
    auto create(const VulkanContext& vulkan_context, const Surface& surface, const Window& window) -> void;

    // return the const reference of the swapchain handle
    auto get() const -> const vk::raii::SwapchainKHR& {
        return handle;
    }

    // create the swapchain image views
    auto create_image_views(const vk::raii::Device& device) -> void;

private:
    vk::raii::SwapchainKHR handle = nullptr;

    std::vector<vk::Image> swapchain_images;
    std::vector<ImageView> swapchain_image_views;

    vk::Format swapchain_image_format;
    vk::Extent2D swapchain_extent;


    // choose a suitable surface format
    auto choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_surface_formats) -> vk::SurfaceFormatKHR;

    // choose a suitable present mode
    auto choose_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) -> vk::PresentModeKHR;

    // set a suitable extent
    auto choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities, const Window& window) -> vk::Extent2D;
};

#endif