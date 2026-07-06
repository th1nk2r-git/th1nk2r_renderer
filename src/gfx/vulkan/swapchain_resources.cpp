#include "gfx/vulkan/swapchain_resources.hpp"

auto SwapchainResources::create(const Context& context, const Window& window) -> void {
    swapchain_.create(context, window);
    swapchain_.create_image_views(context.device());
    render_pass_ = RenderPass(
        context.device(),
        swapchain_.swapchain_image_format()
    );

    create_framebuffers(
        context.device()
    );
}

auto SwapchainResources::create_framebuffers(const Device& device) -> void {
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
