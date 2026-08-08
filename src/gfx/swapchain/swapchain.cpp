#include "gfx/swapchain/swapchain.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>

struct Swapchain::CreateState {
    vk::raii::SwapchainKHR handle;
    std::vector<vk::Image> images;
    vk::Format format;
    vk::Extent2D extent;
};

auto Swapchain::choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_surface_formats) -> vk::SurfaceFormatKHR {
    for (const auto& surface_format : available_surface_formats) {
        if (surface_format.format == vk::Format::eB8G8R8A8Srgb && surface_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return surface_format;
        }
    } 
    return available_surface_formats.at(0);
}

auto Swapchain::choose_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) -> vk::PresentModeKHR {
    for (const auto& present_mode : available_present_modes) {
        if (present_mode == vk::PresentModeKHR::eMailbox) {
            return present_mode;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

auto Swapchain::choose_composite_alpha(
    vk::CompositeAlphaFlagsKHR supported_composite_alpha
) -> vk::CompositeAlphaFlagBitsKHR {
    constexpr std::array preferred_modes{
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit
    };

    for (const auto mode : preferred_modes) {
        if (supported_composite_alpha & mode) {
            return mode;
        }
    }

    throw std::runtime_error(
        "surface does not support any composite alpha mode!"
    );
}

auto Swapchain::choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities, const Window& window) -> vk::Extent2D {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window.get(), &width, &height);
        vk::Extent2D actual_extent (
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );
    actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actual_extent;
}

Swapchain::Swapchain(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) : Swapchain(context.device(), create(context, window, old_swapchain)) {}

Swapchain::Swapchain(const Device& device, CreateState state)
    : handle_(std::move(state.handle)),
      swapchain_images_(std::move(state.images)),
      swapchain_image_views_(create_image_views(
          device,
          swapchain_images_,
          state.format)),
      swapchain_image_format_(state.format),
      swapchain_extent_(state.extent) {}

auto Swapchain::create(
    const DeviceContext& context,
    const Window& window,
    vk::SwapchainKHR old_swapchain
) -> CreateState {
    auto available_surface_formats = context.surface().query_formats(
        context.device().physical_device()
    );

    auto available_present_modes = context.surface().query_present_modes(
        context.device().physical_device()
    );

    auto capabilities = context.surface().query_capabilities(
        context.device().physical_device()
    );

    auto surface_format = choose_surface_format(available_surface_formats);
    auto present_mode = choose_present_mode(available_present_modes);
    auto extent = choose_extent(capabilities, window);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR create_info;
    create_info.surface = context.surface().get();
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    auto graphics_family = context.device().graphics_family();
    auto present_family = context.device().present_family();

    std::vector<uint32_t> queue_family_indices { 
        graphics_family, 
        present_family 
    };
    if (graphics_family != present_family) {
        create_info.imageSharingMode = vk::SharingMode::eConcurrent;
        create_info.setQueueFamilyIndices( queue_family_indices );
    } 
    else {
        create_info.imageSharingMode = vk::SharingMode::eExclusive;
    }

    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = choose_composite_alpha(
        capabilities.supportedCompositeAlpha
    );
    create_info.presentMode = present_mode;
    create_info.clipped = true;
    create_info.oldSwapchain = old_swapchain;
    auto handle = context.device()
                      .logical_device()
                      .createSwapchainKHR(create_info);
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


auto Swapchain::create_image_views(
    const Device& device,
    const std::vector<vk::Image>& images,
    vk::Format format
) -> std::vector<ImageView> {
    std::vector<ImageView> views;
    views.reserve(images.size());

    for (const auto& image : images) {
        views.emplace_back(
            device,
            ImageViewDesc{
                .image = image,
                .format = format
            }
        );
    }

    return views;
}
