#include "rhi/swapchain_context.hpp"

auto SwapchainContext::create(const VulkanContext& vulkan_context, const Surface& surface, const Window& window) -> void {
    swapchain.create(vulkan_context, surface, window);
    swapchain.create_image_views(vulkan_context.get_device());
    render_pass = RenderPass(
        vulkan_context.get_device(),
        swapchain.get_swapchain_image_format()
    );

    create_framebuffers(
        vulkan_context.get_device()
    );
}

auto SwapchainContext::create_framebuffers(const vk::raii::Device& device) -> void {
    framebuffers.clear();

    const auto& swapchain_views = swapchain.get_swapchain_image_views();

    framebuffers.reserve(
        swapchain_views.size()
    );

    for (size_t i = 0; i < swapchain_views.size(); ++i) {
        std::array<vk::ImageView, 1> attachments{
            swapchain_views[i].get()
        };

        framebuffers.emplace_back(
            device,
            render_pass.get(),
            attachments,
            swapchain.get_swapchain_image_extent()
        );
    }
}
