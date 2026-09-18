#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>

#include "core/thread_pool.hpp"
#include "gfx/device/device_context.hpp"
#include "gfx/frame/frames_in_flight.hpp"
#include "gfx/frame/swapchain.hpp"
#include "platform/window.hpp"
#include "render/buffer_registry.hpp"
#include "render/image_registry.hpp"
#include "render/pass/forward/forward_pass.hpp"
#include "render/render_graph.hpp"

class ResourceRegistry;
class Scene;

class Renderer {
public:
    enum class FrameResult {
        Rendered,
        Skipped
    };

    Renderer(
        DeviceContext& device_context,
        Window& window,
        const ResourceRegistry& resources,
        ThreadPool& thread_pool
    );
    ~Renderer() noexcept;

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) = delete;
    auto operator=(Renderer&&) -> Renderer& = delete;

    auto init() -> void;

    auto render(const Scene& scene) -> FrameResult;
    auto wait_idle() const -> void;

private:
    DeviceContext& device_context_;
    Window& window_;
    Swapchain swapchain_;
    FramesInFlight frames_in_flight_;
    ImageRegistry images_;
    BufferRegistry buffers_;
    ForwardPass forward_pass_;
    RenderGraph render_graph_;
    bool initialized_ = false;

    auto init_render_pass() -> void;
    auto build_render_graph() -> void;
    auto recreate_swapchain() -> void;
    auto record_frame(
        const Scene& scene,
        uint32_t image_index
    ) -> void;
    auto submit(uint32_t image_index) -> void;
    auto present(uint32_t image_index) -> vk::Result;
};

#endif
