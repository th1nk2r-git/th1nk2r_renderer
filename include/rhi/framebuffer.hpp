#ifndef FRAMEBUFFER_HPP
#define FRAMEBUFFER_HPP

#include <vulkan/vulkan_raii.hpp>
#include <span>

class Framebuffer {
public:
    Framebuffer() = default;

    Framebuffer(
        const vk::raii::Device& device,
        vk::RenderPass render_pass,
        std::span<const vk::ImageView> attachments,
        vk::Extent2D extent
    ) {
        handle = vk::raii::Framebuffer(
            device,
            vk::FramebufferCreateInfo{}
                .setRenderPass(render_pass)
                .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                .setPAttachments(attachments.data())
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1)
        );
    }

    // return the const reference of the framebuffer
    auto get() const -> const vk::raii::Framebuffer& {
        return handle;
    }

private:
    vk::raii::Framebuffer handle = nullptr;
};

#endif