#ifndef SWAPCHAIN_CONTEXT_HPP
#define SWAPCHAIN_CONTEXT_HPP

#include "gfx/swapchain/swapchain.hpp"
#include "gfx/swapchain/render_pass.hpp"
#include "gfx/swapchain/framebuffer.hpp"

class SwapchainContext {
public:
    SwapchainContext() = default;

    // create the swapchain resources
    auto create(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain = nullptr
    ) -> void;
    
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

    // acquire the next image
    auto acquire(const vk::raii::Semaphore& image_available) const
        -> vk::ResultValue<uint32_t>;

private:
    Swapchain swapchain_;
    RenderPass render_pass_;
    std::vector<Framebuffer> framebuffers_;

    // create the framebuffers
    auto create_framebuffers(const Device& device) -> void;
};

#endif
