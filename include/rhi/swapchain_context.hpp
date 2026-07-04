#ifndef SWAPCHAIN_CONTEXT_HPP
#define SWAPCHAIN_CONTEXT_HPP

#include "rhi/swapchain.hpp"
#include "rhi/render_pass.hpp"
#include "rhi/framebuffer.hpp"

class SwapchainContext {
public:
    SwapchainContext() = default;

    auto create(const VulkanContext& vulkan_context, const Surface& surface, const Window& window) -> void;
    // return the const reference of the swapchain
    auto get_swapchain() const -> const Swapchain& {
        return swapchain;
    }

    // return the const reference of the render pass
    auto get_render_pass() const -> const RenderPass& {
        return render_pass;
    }

    // return the const reference of the framebuffers
    auto get_framebuffers() const -> const std::vector<Framebuffer>& {
        return framebuffers;
    }

private:
    Swapchain swapchain;
    RenderPass render_pass;
    std::vector<Framebuffer> framebuffers;

    auto create_framebuffers(const vk::raii::Device& device) -> void;
};

#endif