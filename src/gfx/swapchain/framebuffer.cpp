#include "gfx/swapchain/framebuffer.hpp"

Framebuffer::Framebuffer(const Device& device, const RenderPass& render_pass, std::span<const ImageView* const> attachments, vk::Extent2D extent) {
    std::vector<vk::ImageView> native_views;
    native_views.reserve(attachments.size());

    for (const ImageView* attachment : attachments) {
        native_views.push_back(attachment->get());
    }

    handle_ = vk::raii::Framebuffer(
        device.logical_device(),
        vk::FramebufferCreateInfo{}
            .setRenderPass(render_pass.get())
            .setAttachmentCount(static_cast<uint32_t>(native_views.size()))
            .setPAttachments(native_views.data())
            .setWidth(extent.width)
            .setHeight(extent.height)
            .setLayers(1));
}
