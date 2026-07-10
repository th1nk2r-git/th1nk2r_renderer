#include "gfx/presentation/swapchain.hpp"

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

auto Swapchain::create(const Context& context, const Window& window) -> void {
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

    this->swapchain_image_format_ = surface_format.format;
    this->swapchain_extent_ = extent;

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
    create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    create_info.presentMode = present_mode;
    create_info.clipped = true;
    create_info.oldSwapchain = nullptr;
    this->handle_ = context.device().logical_device().createSwapchainKHR(create_info);

    this->swapchain_images_ = handle_.getImages();
}


auto Swapchain::create_image_views(const Device& device) -> void {
    swapchain_image_views_.clear();
    swapchain_image_views_.reserve(swapchain_images_.size());

    for (const auto& image : swapchain_images_) {
        swapchain_image_views_.emplace_back(
            device,
            image,
            swapchain_image_format_
        );
    }
}
