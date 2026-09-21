#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "core/thread_pool.hpp"
#include "gfx/device/device_context.hpp"
#include "gfx/frame/frames_in_flight.hpp"
#include "gfx/frame/swapchain.hpp"
#include "platform/window.hpp"
#include "render/pass/render_pass.hpp"
#include "render/render_graph.hpp"

class AssetsDB;
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
        const AssetsDB& assets,
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
    const AssetsDB& assets_;
    Swapchain swapchain_;
    FramesInFlight frames_in_flight_;
    RenderGraph render_graph_;
    std::vector<std::unique_ptr<RenderPass>> render_passes_;

    auto create_render_pass() -> void;
    auto create_render_resources() -> void;
    auto init_render_pass() -> void;
    auto bind_swapchain_images(uint32_t image_index) -> void;
    auto build_render_graph() -> void;
    auto recreate_swapchain() -> void;
    auto record_frame(const Scene& scene, uint32_t image_index) -> void;
    auto submit(uint32_t image_index) -> void;
    auto present(uint32_t image_index) -> vk::Result;
};

#endif
