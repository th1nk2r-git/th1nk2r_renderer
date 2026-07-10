#ifndef FRAMEBUFFER_HPP
#define FRAMEBUFFER_HPP

#include "gfx/core/device.hpp"
#include "gfx/swapchain/image_view.hpp"
#include "gfx/swapchain/render_pass.hpp"
#include <span>

class Framebuffer {
public:
    Framebuffer() = default;

    Framebuffer(const Device& device, const RenderPass& render_pass, std::span<const ImageView* const> attachments, vk::Extent2D extent);

    // return the const reference of the framebuffer
    auto get() const -> const vk::raii::Framebuffer& {
        return handle_;
    }

private:
    vk::raii::Framebuffer handle_ = nullptr;
};

#endif
