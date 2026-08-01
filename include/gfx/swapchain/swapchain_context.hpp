#ifndef SWAPCHAIN_CONTEXT_HPP
#define SWAPCHAIN_CONTEXT_HPP

#include "gfx/swapchain/swapchain.hpp"
#include "gfx/swapchain/render_pass.hpp"
#include "gfx/swapchain/framebuffer.hpp"

class SwapchainContext {
public:
    SwapchainContext() = default;
    SwapchainContext(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain = nullptr
    );
    ~SwapchainContext() = default;

    SwapchainContext(const SwapchainContext&) = delete;
    auto operator=(const SwapchainContext&) -> SwapchainContext& = delete;

    SwapchainContext(SwapchainContext&& other) noexcept = default;
    auto operator=(SwapchainContext&& other) noexcept -> SwapchainContext&;
    
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

    // return the render_finished semaphore of the target swapchain image
    auto render_finished(uint32_t image_index) const -> const vk::raii::Semaphore& {
        return render_finished_[image_index];
    }

    // acquire the next image
    auto acquire(const vk::raii::Semaphore& image_available) const -> vk::ResultValue<uint32_t>;

private:
    Swapchain swapchain_;
    RenderPass render_pass_;
    std::vector<Framebuffer> framebuffers_;
    std::vector<vk::raii::Semaphore> render_finished_;

    // create the framebuffers
    auto create_framebuffers(const Device& device) -> void;

    // create semaphores indexed by swapchain image
    auto create_render_finished(const Device& device) -> void;
};

#endif
