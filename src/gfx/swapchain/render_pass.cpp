#include "gfx/swapchain/render_pass.hpp"

#include <array>

RenderPass::RenderPass(
    const Device& device,
    vk::Format color_format,
    vk::Format depth_format
) {
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

    vk::AttachmentDescription depth_attachment{};
    depth_attachment
        .setFormat(depth_format)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference color_attachment_ref{};
    color_attachment_ref
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    vk::AttachmentReference depth_attachment_ref{};
    depth_attachment_ref
        .setAttachment(1)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::SubpassDescription subpass{};
    subpass
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(color_attachment_ref)
        .setPDepthStencilAttachment(&depth_attachment_ref);

    const auto attachment_stages =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests |
        vk::PipelineStageFlagBits::eLateFragmentTests;

    const auto attachment_writes =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::SubpassDependency dependency{};
    dependency
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(attachment_stages)
        .setSrcAccessMask(attachment_writes)
        .setDstStageMask(attachment_stages)
        .setDstAccessMask(
            attachment_writes |
            vk::AccessFlagBits::eDepthStencilAttachmentRead
        );

    const std::array attachments{
        color_attachment,
        depth_attachment
    };

    vk::RenderPassCreateInfo create_info{};
    this->handle = device.logical_device().createRenderPass(
        create_info
            .setAttachments(attachments)
            .setSubpasses(subpass)
            .setDependencies(dependency)
        );
}
