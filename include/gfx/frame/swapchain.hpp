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

    auto image_count() const noexcept -> uint32_t {
        return static_cast<uint32_t>(images_.size());
    }

    auto extent() const noexcept -> vk::Extent2D {
        return extent_;
    }

    auto image(uint32_t image_index) noexcept -> Image& {
        return images_.at(image_index);
    }

    auto image(uint32_t image_index) const -> const Image& {
        return images_.at(image_index);
    }

    auto depth_image(uint32_t image_index) noexcept -> Image& {
        return depth_images_.at(image_index);
    }

    auto depth_image(uint32_t image_index) const -> const Image& {
        return depth_images_.at(image_index);
    }

    auto render_finished(uint32_t image_index) const
        -> const vk::raii::Semaphore& {
        return render_finished_.at(image_index);
    }

    auto acquire(const vk::raii::Semaphore& image_available) const -> vk::ResultValue<uint32_t>;

private:
    struct CreateState;

    Swapchain(const DeviceContext& context, CreateState state);

    vk::raii::SwapchainKHR handle_ = nullptr;
    std::vector<Image> images_;
    vk::Format image_format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{};

    vk::Format depth_format_ = vk::Format::eUndefined;
    std::vector<Image> depth_images_;
    std::vector<vk::raii::Semaphore> render_finished_;

    static auto create(
        const DeviceContext& context,
        const Window& window,
        vk::SwapchainKHR old_swapchain
    ) -> CreateState;
};

#endif
