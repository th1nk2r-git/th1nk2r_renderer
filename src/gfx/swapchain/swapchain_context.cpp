#include "gfx/swapchain/swapchain_context.hpp"

#include <limits>

auto SwapchainContext::create(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) -> void {
    swapchain_.create(context, window, old_swapchain);
    swapchain_.create_image_views(context.device());
    render_pass_ = RenderPass(
        context.device(),
        swapchain_.swapchain_image_format()
    );

    create_framebuffers(
        context.device()
    );
}

auto SwapchainContext::create_framebuffers(const Device& device) -> void {
    framebuffers_.clear();

    const auto& swapchain_views = swapchain_.swapchain_image_views();

    framebuffers_.reserve(
        swapchain_views.size()
    );

    for (size_t i = 0; i < swapchain_views.size(); ++i) {
        std::array<const ImageView* const, 1> attachments{
            &swapchain_views[i]
        };

        framebuffers_.emplace_back(
            device,
            render_pass_,
            attachments,
            swapchain_.swapchain_image_extent()
        );
    }
}

auto SwapchainContext::acquire(const vk::raii::Semaphore& image_available) const
    -> vk::ResultValue<uint32_t> {
    return swapchain_.get().acquireNextImage(
        std::numeric_limits<uint64_t>::max(),
        *image_available
    );
}
