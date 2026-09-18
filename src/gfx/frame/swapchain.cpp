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

    auto create_images(
        const Device& device,
        const std::vector<vk::Image>& images,
        vk::Format format,
        vk::Extent2D extent
    ) -> std::vector<Image> {
        std::vector<Image> resources;
        resources.reserve(images.size());
        for (const auto image : images) {
            resources.push_back(
                Image::external(
                    device,
                    image,
                    ImageDesc{
                        .format = format,
                        .extent = vk::Extent3D{
                            extent.width,
                            extent.height,
                            1
                        },
                        .usage = vk::ImageUsageFlagBits::eColorAttachment,
                        .initial_layout = vk::ImageLayout::eUndefined,
                        .final_layout = vk::ImageLayout::ePresentSrcKHR
                    }
                )
            );
        }
        return resources;
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

    auto create_depth_images(
        const Device& device,
        const MemoryAllocator& allocator,
        size_t image_count,
        vk::Extent2D extent,
        vk::Format format
    ) -> std::vector<Image> {
        std::vector<Image> images;
        images.reserve(image_count);
        for (size_t index = 0; index < image_count; ++index) {
            images.emplace_back(
                device,
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
      images_(
          create_images(
              context.device(),
              state.images,
              state.format,
              state.extent
          )
      ),
      image_format_(state.format),
      extent_(state.extent),
      depth_format_(choose_depth_format(context.device())),
      depth_images_(
          create_depth_images(
              context.device(),
              context.allocator(),
              images_.size(),
              extent_,
              depth_format_
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
