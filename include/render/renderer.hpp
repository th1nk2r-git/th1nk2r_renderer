#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "gfx/device/device_context.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/pipeline/pipeline_state.hpp"
#include "gfx/resource/model.hpp"
#include "gfx/resource/sampler.hpp"
#include "gfx/resource/texture2d.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "gfx/descriptor/descriptor_pool.hpp"
#include "platform/window.hpp"
#include "render/material_context.hpp"
#include "render/uniforms_context.hpp"

class Renderer {
public:
    Renderer() = delete;
    explicit Renderer(const Window& window);
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) = delete;
    auto operator=(Renderer&&) -> Renderer& = delete;

    // render a frame
    auto render() -> void;

    // wait gpu
    auto wait_idle() const -> void {
        device_context_.device().logical_device().waitIdle();
    }

    // recreate the swapchain
    auto recreate_swapchain(const Window& window) -> void;

private:
    DeviceContext device_context_;
    SwapchainContext swapchain_context_;
    FrameContext frame_context_;
    DescriptorPool descriptor_pool_;
    PipelineState main_pipeline_;
    UniformsContext uniforms_context_;

    Model model_;
    Texture2D texture_;
    Sampler sampler_;
    MaterialContext material_context_;

    // update the ubo per frame
    auto update_ubo() -> void;

    // recourd the command
    auto record(uint32_t image_id) -> void;

    // submit the command
    auto submit(uint32_t image_id) -> void;

    // present the image
    auto present(uint32_t image_id) -> vk::Result;
};

#endif
