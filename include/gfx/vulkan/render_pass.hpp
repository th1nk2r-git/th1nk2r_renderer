#ifndef RENDER_PASS_HPP
#define RENDER_PASS_HPP

#include "gfx/vulkan/device.hpp"

class RenderPass {
public:
    RenderPass() = default;
    
    RenderPass(const Device& device, const vk::Format& color_format) {
        vk::AttachmentDescription color_attachment{};
        color_attachment
            .setFormat(color_format)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

        vk::AttachmentReference color_attachment_ref{};
        color_attachment_ref
            .setAttachment(0)
            .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::SubpassDescription subpass{};
        subpass
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setColorAttachments(color_attachment_ref);

        vk::SubpassDependency dependency{};
        dependency
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
            .setSrcAccessMask({})
            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

        vk::RenderPassCreateInfo create_info{};
        this->handle = device.logical_device().createRenderPass(
        create_info
            .setAttachments(color_attachment)
            .setSubpasses(subpass)
            .setDependencies(dependency)
        );
    }
    
    ~RenderPass() = default;

    // return the const reference of the render pass
    auto get() const -> const vk::raii::RenderPass& {
        return handle;
    }

private:
    vk::raii::RenderPass handle = nullptr;
};

#endif