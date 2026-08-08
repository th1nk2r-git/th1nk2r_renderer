#include "gfx/swapchain/swapchain_context.hpp"

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
    auto choose_depth_format(const Device& device) -> vk::Format {
        constexpr std::array candidates{
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint
        };
        for (const auto format : candidates) {
            const auto properties = device.physical_device().getFormatProperties(format);
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

    auto create_depth_images(
        const GpuAllocator& allocator,
        size_t image_count,
        vk::Extent2D extent,
        vk::Format format
    ) -> std::vector<Image> {
        std::vector<Image> images;
        images.reserve(image_count);

        for (size_t i = 0; i < image_count; ++i) {
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
    ) -> std::vector<ImageView> {
        std::vector<ImageView> views;
        views.reserve(images.size());

        for (const auto& image : images) {
            views.emplace_back(
                device,
                ImageViewDesc{
                    .image = image.get(),
                    .format = format,
                    .aspect_flags = depth_aspect_flags(format)
                }
            );
        }
        return views;
    }

    auto create_framebuffers(
        const Device& device,
        const RenderPass& render_pass,
        const Swapchain& swapchain,
        const std::vector<ImageView>& depth_image_views
    ) -> std::vector<Framebuffer> {
        const auto& swapchain_views = swapchain.swapchain_image_views();
        std::vector<Framebuffer> framebuffers;
        framebuffers.reserve(swapchain_views.size());

        for (size_t i = 0; i < swapchain_views.size(); ++i) {
            const std::array<const ImageView* const, 2> attachments{
                &swapchain_views[i],
                &depth_image_views[i]
            };
            framebuffers.emplace_back(
                device,
                render_pass,
                attachments,
                swapchain.swapchain_image_extent()
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

        const vk::SemaphoreCreateInfo create_info{};
        for (size_t i = 0; i < image_count; ++i) {
            semaphores.emplace_back(
                device.logical_device().createSemaphore(create_info)
            );
        }
        return semaphores;
    }
}

SwapchainContext::SwapchainContext(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) : swapchain_(context, window, old_swapchain),
    depth_format_(choose_depth_format(context.device())),
    render_pass_(
        context.device(),
        swapchain_.swapchain_image_format(),
        depth_format_
    ),
    depth_images_(
        create_depth_images(
            context.allocator(),
            swapchain_.swapchain_image_views().size(),
            swapchain_.swapchain_image_extent(),
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
            swapchain_,
            depth_image_views_
        )
    ),
    render_finished_(
        create_render_finished(
            context.device(),
            swapchain_.swapchain_image_views().size()
        )
    ) {}

auto SwapchainContext::operator=(SwapchainContext&& other) noexcept -> SwapchainContext& {
    if (this == &other) {
        return *this;
    }

    std::destroy_at(this);
    std::construct_at(this, std::move(other));
    return *this;
}

auto SwapchainContext::acquire(const vk::raii::Semaphore& image_available) const -> vk::ResultValue<uint32_t> {
    return swapchain_.get().acquireNextImage(
        std::numeric_limits<uint64_t>::max(),
        *image_available
    );
}
