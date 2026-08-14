#include "gfx/frame/swapchain.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

struct Swapchain::CreateState {
    vk::raii::SwapchainKHR handle;
    std::vector<vk::Image> images;
    vk::Format format;
    vk::Extent2D extent;
};

namespace {
    auto choose_surface_format(
        const std::vector<vk::SurfaceFormatKHR>& available_formats
    ) -> vk::SurfaceFormatKHR {
        for (const auto& surface_format : available_formats) {
            if (surface_format.format == vk::Format::eB8G8R8A8Srgb &&
                surface_format.colorSpace ==
                    vk::ColorSpaceKHR::eSrgbNonlinear) {
                return surface_format;
            }
        }
        return available_formats.at(0);
    }

    auto choose_present_mode(
        const std::vector<vk::PresentModeKHR>& available_modes
    ) -> vk::PresentModeKHR {
        for (const auto present_mode : available_modes) {
            if (present_mode == vk::PresentModeKHR::eMailbox) {
                return present_mode;
            }
        }
        return vk::PresentModeKHR::eFifo;
    }

    auto choose_composite_alpha(vk::CompositeAlphaFlagsKHR supported)
        -> vk::CompositeAlphaFlagBitsKHR {
        constexpr std::array preferred_modes{
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
            vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
            vk::CompositeAlphaFlagBitsKHR::eInherit
        };
        for (const auto mode : preferred_modes) {
            if (supported & mode) {
                return mode;
            }
        }
        throw std::runtime_error(
            "surface does not support any composite alpha mode!"
        );
    }

    auto choose_extent(
        const vk::SurfaceCapabilitiesKHR& capabilities,
        const Window& window
    ) -> vk::Extent2D {
        if (capabilities.currentExtent.width !=
            std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window.get(), &width, &height);
        vk::Extent2D extent{
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        extent.width = std::clamp(
            extent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        extent.height = std::clamp(
            extent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );
        return extent;
    }

    auto create_image_views(
        const Device& device,
        const std::vector<vk::Image>& images,
        vk::Format format
    ) -> std::vector<vk::raii::ImageView> {
        std::vector<vk::raii::ImageView> views;
        views.reserve(images.size());
        for (const auto image : images) {
            vk::ImageViewCreateInfo create_info{};
            create_info
                .setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setComponents(vk::ComponentMapping{
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity
                })
                .setSubresourceRange(vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor,
                    0,
                    1,
                    0,
                    1
                });
            views.emplace_back(
                device.logical_device().createImageView(create_info)
            );
        }
        return views;
    }

    auto choose_depth_format(const Device& device) -> vk::Format {
        constexpr std::array candidates{
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint
        };
        for (const auto format : candidates) {
            const auto properties =
                device.physical_device().getFormatProperties(format);
            if (properties.optimalTilingFeatures &
                vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return format;
            }
        }
        throw std::runtime_error(
            "failed to find a supported depth image format!"
        );
    }

    auto depth_aspect_flags(vk::Format format) -> vk::ImageAspectFlags {
        vk::ImageAspectFlags flags = vk::ImageAspectFlagBits::eDepth;
        if (format == vk::Format::eD32SfloatS8Uint ||
            format == vk::Format::eD24UnormS8Uint) {
            flags |= vk::ImageAspectFlagBits::eStencil;
        }
        return flags;
    }

    auto create_render_pass(
        const Device& device,
        vk::Format color_format,
        vk::Format depth_format
    ) -> vk::raii::RenderPass {
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

        vk::AttachmentReference color_reference{};
        color_reference
            .setAttachment(0)
            .setLayout(vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference depth_reference{};
        depth_reference
            .setAttachment(1)
            .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::SubpassDescription subpass{};
        subpass
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setColorAttachments(color_reference)
            .setPDepthStencilAttachment(&depth_reference);

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

        const std::array attachments{color_attachment, depth_attachment};
        vk::RenderPassCreateInfo create_info{};
        create_info
            .setAttachments(attachments)
            .setSubpasses(subpass)
            .setDependencies(dependency);
        return device.logical_device().createRenderPass(create_info);
    }

    auto create_depth_images(
        const MemoryAllocator& allocator,
        size_t image_count,
        vk::Extent2D extent,
        vk::Format format
    ) -> std::vector<Image> {
        std::vector<Image> images;
        images.reserve(image_count);
        for (size_t index = 0; index < image_count; ++index) {
            images.emplace_back(
                allocator,
                ImageDesc{
                    .format = format,
                    .extent = vk::Extent3D{extent.width, extent.height, 1},
                    .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
                }
            );
        }
        return images;
    }

    auto create_depth_image_views(
        const Device& device,
        const std::vector<Image>& images,
        vk::Format format
    ) -> std::vector<vk::raii::ImageView> {
        std::vector<vk::raii::ImageView> views;
        views.reserve(images.size());
        for (const auto& image : images) {
            vk::ImageViewCreateInfo create_info{};
            create_info
                .setImage(image.get())
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setSubresourceRange(vk::ImageSubresourceRange{
                    depth_aspect_flags(format),
                    0,
                    1,
                    0,
                    1
                });
            views.emplace_back(
                device.logical_device().createImageView(create_info)
            );
        }
        return views;
    }

    auto create_framebuffers(
        const Device& device,
        const vk::raii::RenderPass& render_pass,
        const std::vector<vk::raii::ImageView>& color_views,
        const std::vector<vk::raii::ImageView>& depth_views,
        vk::Extent2D extent
    ) -> std::vector<vk::raii::Framebuffer> {
        std::vector<vk::raii::Framebuffer> framebuffers;
        framebuffers.reserve(color_views.size());
        for (size_t index = 0; index < color_views.size(); ++index) {
            const std::array attachments{
                *color_views[index],
                *depth_views[index]
            };
            vk::FramebufferCreateInfo create_info{};
            create_info
                .setRenderPass(*render_pass)
                .setAttachments(attachments)
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1);
            framebuffers.emplace_back(
                device.logical_device().createFramebuffer(create_info)
            );
        }
        return framebuffers;
    }

    auto create_render_finished(
        const Device& device,
        size_t image_count
    ) -> std::vector<vk::raii::Semaphore> {
        std::vector<vk::raii::Semaphore> semaphores;
        semaphores.reserve(image_count);
        for (size_t index = 0; index < image_count; ++index) {
            semaphores.emplace_back(
                device.logical_device().createSemaphore(
                    vk::SemaphoreCreateInfo{}
                )
            );
        }
        return semaphores;
    }
}

Swapchain::Swapchain(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) : Swapchain(context, create(context, window, old_swapchain)) {}

Swapchain::Swapchain(const DeviceContext& context, CreateState state)
    : handle_(std::move(state.handle)),
      images_(std::move(state.images)),
      image_views_(
          create_image_views(context.device(), images_, state.format)
      ),
      image_format_(state.format),
      extent_(state.extent),
      depth_format_(choose_depth_format(context.device())),
      render_pass_(
          create_render_pass(context.device(), image_format_, depth_format_)
      ),
      depth_images_(
          create_depth_images(
              context.allocator(),
              images_.size(),
              extent_,
              depth_format_
          )
      ),
      depth_image_views_(
          create_depth_image_views(
              context.device(),
              depth_images_,
              depth_format_
          )
      ),
      framebuffers_(
          create_framebuffers(
              context.device(),
              render_pass_,
              image_views_,
              depth_image_views_,
              extent_
          )
      ),
      render_finished_(
          create_render_finished(context.device(), images_.size())
      ) {}

auto Swapchain::create(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) -> CreateState {
    const auto& physical_device = context.device().physical_device();
    const auto available_formats =
        physical_device.getSurfaceFormatsKHR(*context.surface());
    const auto available_present_modes =
        physical_device.getSurfacePresentModesKHR(*context.surface());
    const auto capabilities =
        physical_device.getSurfaceCapabilitiesKHR(*context.surface());

    const auto surface_format = choose_surface_format(available_formats);
    const auto present_mode = choose_present_mode(available_present_modes);
    const auto extent = choose_extent(capabilities, window);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR create_info{};
    create_info
        .setSurface(*context.surface())
        .setMinImageCount(image_count)
        .setImageFormat(surface_format.format)
        .setImageColorSpace(surface_format.colorSpace)
        .setImageExtent(extent)
        .setImageArrayLayers(1)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
        .setPreTransform(capabilities.currentTransform)
        .setCompositeAlpha(
            choose_composite_alpha(capabilities.supportedCompositeAlpha)
        )
        .setPresentMode(present_mode)
        .setClipped(true)
        .setOldSwapchain(old_swapchain);

    const auto graphics_family = context.device().graphics_family();
    const auto present_family = context.device().present_family();
    const std::array queue_family_indices{
        graphics_family,
        present_family
    };
    if (graphics_family != present_family) {
        create_info
            .setImageSharingMode(vk::SharingMode::eConcurrent)
            .setQueueFamilyIndices(queue_family_indices);
    }
    else {
        create_info.setImageSharingMode(vk::SharingMode::eExclusive);
    }

    auto handle =
        context.device().logical_device().createSwapchainKHR(create_info);
    auto images = handle.getImages();
    return CreateState{
        .handle = std::move(handle),
        .images = std::move(images),
        .format = surface_format.format,
        .extent = extent
    };
}

auto Swapchain::operator=(Swapchain&& other) noexcept -> Swapchain& {
    if (this == &other) {
        return *this;
    }
    std::destroy_at(this);
    std::construct_at(this, std::move(other));
    return *this;
}

auto Swapchain::acquire(const vk::raii::Semaphore& image_available) const
    -> vk::ResultValue<uint32_t> {
    return handle_.acquireNextImage(
        std::numeric_limits<uint64_t>::max(),
        *image_available
    );
}
