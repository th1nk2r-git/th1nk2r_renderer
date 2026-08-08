#include "gfx/swapchain/framebuffer.hpp"

namespace {
    auto create_framebuffer(
        const Device& device,
        const RenderPass& render_pass,
        std::span<const ImageView* const> attachments,
        vk::Extent2D extent
    ) -> vk::raii::Framebuffer {
        std::vector<vk::ImageView> native_views;
        native_views.reserve(attachments.size());

        for (const ImageView* attachment : attachments) {
            native_views.push_back(attachment->get());
        }

        return vk::raii::Framebuffer(
            device.logical_device(),
            vk::FramebufferCreateInfo{}
                .setRenderPass(render_pass.get())
                .setAttachmentCount(static_cast<uint32_t>(native_views.size()))
                .setPAttachments(native_views.data())
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1)
            );
    }
}

Framebuffer::Framebuffer(
    const Device& device,
    const RenderPass& render_pass,
    std::span<const ImageView* const> attachments,
    vk::Extent2D extent
) : handle_(create_framebuffer(device, render_pass, attachments, extent)) {}
