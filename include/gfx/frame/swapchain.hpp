#ifndef SWAPCHAIN_HPP
#define SWAPCHAIN_HPP

#include <cstdint>
#include <vector>

#include "gfx/device/device_context.hpp"
#include "gfx/resource/image.hpp"
#include "platform/window.hpp"

class Swapchain {
public:
    Swapchain() = default;
    Swapchain(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain = nullptr
    );

    Swapchain(const Swapchain&) = delete;
    auto operator=(const Swapchain&) -> Swapchain& = delete;
    Swapchain(Swapchain&&) noexcept = default;
    auto operator=(Swapchain&& other) noexcept -> Swapchain&;

    auto handle() const noexcept -> const vk::raii::SwapchainKHR& {
        return handle_;
    }

    auto image_format() const noexcept -> vk::Format {
        return image_format_;
    }

    auto extent() const noexcept -> vk::Extent2D {
        return extent_;
    }

    auto render_pass() const noexcept -> const vk::raii::RenderPass& {
        return render_pass_;
    }

    auto framebuffers() const noexcept -> const std::vector<vk::raii::Framebuffer>& {
        return framebuffers_;
    }

    auto render_finished(uint32_t image_index) const
        -> const vk::raii::Semaphore& {
        return render_finished_.at(image_index);
    }

    auto compatible_with(const Swapchain& other) const noexcept -> bool {
        return image_format_ == other.image_format_ && depth_format_ == other.depth_format_;
    }

    auto acquire(const vk::raii::Semaphore& image_available) const -> vk::ResultValue<uint32_t>;

private:
    struct CreateState;

    Swapchain(const DeviceContext& context, CreateState state);

    vk::raii::SwapchainKHR handle_ = nullptr;
    std::vector<vk::Image> images_;
    std::vector<vk::raii::ImageView> image_views_;
    vk::Format image_format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{};

    vk::Format depth_format_ = vk::Format::eUndefined;
    vk::raii::RenderPass render_pass_ = nullptr;
    std::vector<Image> depth_images_;
    std::vector<vk::raii::ImageView> depth_image_views_;
    std::vector<vk::raii::Framebuffer> framebuffers_;
    std::vector<vk::raii::Semaphore> render_finished_;

    static auto create(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain
    ) -> CreateState;
};

#endif
