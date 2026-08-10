#ifndef RENDER_PASS_HPP
#define RENDER_PASS_HPP

#include "gfx/device/device.hpp"

class RenderPass {
public:
    RenderPass() = default;
    
    RenderPass(
        const Device& device,
        vk::Format color_format,
        vk::Format depth_format
    );

    // return the const reference of the render pass
    auto get() const -> const vk::raii::RenderPass& {
        return handle_;
    }

    auto compatible_with(const RenderPass& other) const noexcept -> bool {
        return color_format_ == other.color_format_ &&
               depth_format_ == other.depth_format_;
    }

private:
    vk::Format color_format_ = vk::Format::eUndefined;
    vk::Format depth_format_ = vk::Format::eUndefined;
    vk::raii::RenderPass handle_ = nullptr;
};

#endif
