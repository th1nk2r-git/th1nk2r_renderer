#ifndef SWAPCHAIN_HPP
#define SWAPCHAIN_HPP

#include "rhi/vulkan_context.hpp"
#include "rhi/surface.hpp"
#include "rhi/image_view.hpp"

class Swapchain {
public:
    Swapchain() = default;

    // create the swapchain
    auto create(const VulkanContext& vulkan_context, const Surface& surface, const Window& window) -> void;

    // create the swapchain image views
    auto create_image_views(const vk::raii::Device& device) -> void;

    // return the const reference of the swapchain
    auto get() const -> const vk::raii::SwapchainKHR& {
        return handle;
    }

    // return the const reference of the swapchain image format
    auto get_swapchain_image_format() const -> const vk::Format& {
        return swapchain_image_format;
    }

    // return the const reference of the swapchain image extent
    auto get_swapchain_image_extent() const -> const vk::Extent2D& {
        return swapchain_extent;
    }

    // return the const reference of the swapchain image views
    auto get_swapchain_image_views() const -> const std::vector<ImageView>& {
        return swapchain_image_views;
    }

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