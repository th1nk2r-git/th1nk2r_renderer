#ifndef SWAPCHAIN_CONTEXT_HPP
#define SWAPCHAIN_CONTEXT_HPP

#include "gfx/vulkan/swapchain.hpp"
#include "gfx/vulkan/render_pass.hpp"
#include "gfx/vulkan/framebuffer.hpp"

class SwapchainResources {
public:
    SwapchainResources() = default;

    // create the swapchain resources
    auto create(const Context& context, const Window& window) -> void;
    
    // return the const reference of the swapchain
    auto get_swapchain() const -> const Swapchain& {
        return swapchain_;
    }

    // return the const reference of the render pass
    auto get_render_pass() const -> const RenderPass& {
        return render_pass_;
    }

    // return the const reference of the framebuffers
    auto get_framebuffers() const -> const std::vector<Framebuffer>& {
        return framebuffers_;
    }

private:
    Swapchain swapchain_;
    RenderPass render_pass_;
    std::vector<Framebuffer> framebuffers_;

    // create the framebuffers
    auto create_framebuffers(const Device& device) -> void;
};

#endif
