#ifndef RENDER_PASS_HPP
#define RENDER_PASS_HPP

#include "gfx/core/device.hpp"

class RenderPass {
public:
    RenderPass() = default;
    
    RenderPass(const Device& device, const vk::Format& color_format);

    // return the const reference of the render pass
    auto get() const -> const vk::raii::RenderPass& {
        return handle;
    }

private:
    vk::raii::RenderPass handle = nullptr;
};

#endif
