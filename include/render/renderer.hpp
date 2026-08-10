#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "gfx/device/device_context.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/frame/camera_uniforms.hpp"
#include "gfx/pipeline/pipeline_state.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "platform/window.hpp"
#include "scene/scene.hpp"

class ResourceRegistry;

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
    auto render(const Scene& scene) -> void;

    // wait gpu
    auto wait_idle() const -> void {
        device_context_.device().logical_device().waitIdle();
    }

    // recreate the swapchain
    auto recreate_swapchain(const Window& window) -> void;

    // attach the registry used to resolve resources while rendering
    auto attach_registry(const ResourceRegistry& registry) -> void;

    auto device() const noexcept -> const Device& {
        return device_context_.device();
    }

    auto allocator() const noexcept -> const GpuAllocator& {
        return device_context_.allocator();
    }

    auto uploader() noexcept -> DataUploader& {
        return device_context_.uploader();
    }

    auto material_descriptor_set_layout() const -> const DescriptorSetLayout& {
        return main_pipeline_.descriptor_set_layout(1);
    }

private:
    DeviceContext device_context_;
    SwapchainContext swapchain_context_;
    FrameContext frame_context_;
    PipelineState main_pipeline_;
    CameraUniforms camera_uniforms_;
    const ResourceRegistry* registry_ = nullptr;

    // recourd the command
    auto record(uint32_t image_id, const Scene& scene) -> void;

    // submit the command
    auto submit(uint32_t image_id) -> void;

    // present the image
    auto present(uint32_t image_id) -> vk::Result;
};

#endif
