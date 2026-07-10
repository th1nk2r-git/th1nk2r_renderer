#ifndef SWAPCHAIN_RESOURCES_HPP
#define SWAPCHAIN_RESOURCES_HPP

#include "gfx/presentation/swapchain.hpp"
#include "gfx/presentation/render_pass.hpp"
#include "gfx/presentation/framebuffer.hpp"

class SwapchainResources {
public:
    SwapchainResources() = default;

    // create the swapchain resources
    auto create(const Context& context, const Window& window) -> void;
    
    // return the const reference of the swapchain
    auto swapchain() const -> const Swapchain& {
        return swapchain_;
    }

    // return the const reference of the render pass
    auto render_pass() const -> const RenderPass& {
        return render_pass_;
    }

    // return the const reference of the framebuffers
    auto framebuffers() const -> const std::vector<Framebuffer>& {
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
