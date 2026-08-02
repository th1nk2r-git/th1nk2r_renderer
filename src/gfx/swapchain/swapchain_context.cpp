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
            const auto properties = device
                                        .physical_device()
                                        .getFormatProperties(format);

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
}

SwapchainContext::SwapchainContext(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) {
    swapchain_ = Swapchain(context, window, old_swapchain);
    depth_format_ = choose_depth_format(context.device());
    render_pass_ = RenderPass(
        context.device(),
        swapchain_.swapchain_image_format(),
        depth_format_
    );

    create_depth_resources(
        context.device(),
        context.allocator()
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

auto SwapchainContext::create_depth_resources(
    const Device& device,
    const GpuAllocator& allocator
) -> void {
    depth_image_views_.clear();
    depth_images_.clear();

    const auto image_count = swapchain_.swapchain_image_views().size();
    const auto swapchain_extent = swapchain_.swapchain_image_extent();

    depth_images_.reserve(image_count);
    depth_image_views_.reserve(image_count);

    for (size_t i = 0; i < image_count; ++i) {
        depth_images_.emplace_back(
            allocator,
            ImageDesc{
                .format = depth_format_,
                .extent = vk::Extent3D{
                    swapchain_extent.width,
                    swapchain_extent.height,
                    1
                },
                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
            }
        );

        depth_image_views_.emplace_back(
            device,
            ImageViewDesc{
                .image = depth_images_.back().get(),
                .format = depth_format_,
                .aspect_flags = depth_aspect_flags(depth_format_)
            }
        );
    }
}

auto SwapchainContext::create_framebuffers(const Device& device) -> void {
    framebuffers_.clear();

    const auto& swapchain_views = swapchain_.swapchain_image_views();

    framebuffers_.reserve(
        swapchain_views.size()
    );

    for (size_t i = 0; i < swapchain_views.size(); ++i) {
        std::array<const ImageView* const, 2> attachments{
            &swapchain_views[i],
            &depth_image_views_[i]
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
