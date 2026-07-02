#ifndef SWAPCHAIN
#define SWAPCHAIN

#include "vulkan_context.hpp"
#include "surface.hpp"

class Swapchain {
public:
    Swapchain() = default;

    // create the swapchain
    auto create(const VulkanContext& vulkan_context, const Surface& surface) -> void;

    // return the const reference of the swapchain handle
    auto get() const -> const vk::raii::SwapchainKHR& {
        return handle;
    }

private:
    vk::raii::SwapchainKHR handle = nullptr;
};

#endif