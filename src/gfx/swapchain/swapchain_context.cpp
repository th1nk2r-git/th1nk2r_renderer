#include "gfx/swapchain/swapchain_context.hpp"

#include <limits>
#include <memory>
#include <utility>

SwapchainContext::SwapchainContext(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) {
    swapchain_ = Swapchain(context, window, old_swapchain);
    render_pass_ = RenderPass(
        context.device(),
        swapchain_.swapchain_image_format()
    );

    create_framebuffers(
        context.device()
    );
    create_render_finished(
        context.device()
    );
}

auto SwapchainContext::operator=(SwapchainContext&& other) noexcept -> SwapchainContext& {
    if (this == &other) {
        return *this;
    }

    std::destroy_at(this);
    std::construct_at(this, std::move(other));
    return *this;
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

auto SwapchainContext::create_render_finished(const Device& device) -> void {
    render_finished_.clear();

    const auto image_count = swapchain_.swapchain_image_views().size();
    render_finished_.reserve(image_count);

    const vk::SemaphoreCreateInfo create_info{};
    for (size_t i = 0; i < image_count; ++i) {
        render_finished_.emplace_back(
            device.logical_device().createSemaphore(create_info)
        );
    }
}

auto SwapchainContext::acquire(const vk::raii::Semaphore& image_available) const -> vk::ResultValue<uint32_t> {
    return swapchain_.get().acquireNextImage(
        std::numeric_limits<uint64_t>::max(),
        *image_available
    );
}
