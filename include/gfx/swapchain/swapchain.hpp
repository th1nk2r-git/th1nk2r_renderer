#ifndef SWAPCHAIN_HPP
#define SWAPCHAIN_HPP

#include "gfx/device/device_context.hpp"
#include "gfx/device/surface.hpp"
#include "gfx/resource/image_view.hpp"

class Swapchain {
public:
    Swapchain() = default;
    Swapchain(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain = nullptr
    );
    ~Swapchain() = default;

    Swapchain(const Swapchain&) = delete;
    auto operator=(const Swapchain&) -> Swapchain& = delete;
    Swapchain(Swapchain&& other) noexcept = default;
    auto operator=(Swapchain&& other) noexcept -> Swapchain&;

    // return the const reference of the swapchain
    auto get() const -> const vk::raii::SwapchainKHR& {
        return handle_;
    }

    // return the const reference of the swapchain image format
    auto swapchain_image_format() const -> const vk::Format& {
        return swapchain_image_format_;
    }

    // return the const reference of the swapchain image extent
    auto swapchain_image_extent() const -> const vk::Extent2D& {
        return swapchain_extent_;
    }

    // return the const reference of the swapchain image views
    auto swapchain_image_views() const -> const std::vector<ImageView>& {
        return swapchain_image_views_;
    }

private:
    struct CreateState;

    Swapchain(const Device& device, CreateState state);

    vk::raii::SwapchainKHR handle_ = nullptr;

    std::vector<vk::Image> swapchain_images_;
    std::vector<ImageView> swapchain_image_views_;

    vk::Format swapchain_image_format_;
    vk::Extent2D swapchain_extent_;

    // create all data that must exist before member initialization
    static auto create(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain
    ) -> CreateState;

    // create image views
    static auto create_image_views(
        const Device& device,
        const std::vector<vk::Image>& images,
        vk::Format format
    ) -> std::vector<ImageView>;

    // choose a suitable surface format
    static auto choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_surface_formats) -> vk::SurfaceFormatKHR;

    // choose a suitable present mode
    static auto choose_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) -> vk::PresentModeKHR;

    // choose a supported composite alpha mode
    static auto choose_composite_alpha(vk::CompositeAlphaFlagsKHR supported_composite_alpha) -> vk::CompositeAlphaFlagBitsKHR;

    // set a suitable extent
    static auto choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities, const Window& window) -> vk::Extent2D;
};

#endif
