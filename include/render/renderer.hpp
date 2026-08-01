#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <array>

#include "gfx/device/device_context.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/pipeline/pipeline_state.hpp"
#include "gfx/resources/buffer.hpp"
#include "gfx/resources/data_uploader.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "gfx/descriptor/descriptor_pool.hpp"
#include "platform/window.hpp"
#include "render/camera_uniforms_context.hpp"
#include "render/render_mesh.hpp"

class Renderer {
public:
    Renderer() = delete;
    explicit Renderer(const Window& window);
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;

    Renderer(Renderer&& other) = delete;
    auto operator=(Renderer&& other) noexcept -> Renderer& = delete;

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
    DataUploader data_uploader_;
    SwapchainContext swapchain_context_;
    FrameContext frame_context_;
    DescriptorPool descriptor_pool_;
    PipelineState main_pipeline_;
    CameraUniformsContext camera_uniforms_context_;

    Mesh mesh_;
    RenderMesh render_mesh_;

    // create the default mesh
    auto create_mesh() -> void;

    // recourd the command
    auto record(uint32_t image_id) -> void;

    // submit the command
    auto submit(uint32_t image_id) -> void;

    // present the image
    auto present(uint32_t image_id) -> vk::Result;
};

#endif
