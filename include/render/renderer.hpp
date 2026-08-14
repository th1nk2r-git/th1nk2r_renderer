#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <concepts>
#include <cstdint>
#include <utility>

#include "gfx/frame/frames_in_flight.hpp"
#include "gfx/frame/swapchain.hpp"

struct RenderFrameContext {
    vk::raii::CommandBuffer& command_buffer;
    const vk::raii::RenderPass& render_pass;
    const vk::raii::Framebuffer& framebuffer;
    vk::Extent2D extent;
    uint32_t frame_index = 0;
};

class Renderer {
public:
    Renderer() = delete;
    Renderer(const DeviceContext& device_context, const Window& window);

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) = delete;
    auto operator=(Renderer&&) -> Renderer& = delete;

    template <typename Recorder> requires std::invocable<Recorder&&, const RenderFrameContext&>
    auto render(const Device& device, Recorder&& recorder) -> void {
        frames_in_flight_.wait(device);

        auto& image_available = frames_in_flight_.current_frame().image_available;
        const auto acquire_result = swapchain_.acquire(image_available);
        const auto image_id = acquire_result.value;
        const auto swapchain_suboptimal = acquire_result.result == vk::Result::eSuboptimalKHR;

        record(image_id, std::forward<Recorder>(recorder));
        submit(device, image_id);
        const auto present_result = present(device, image_id);

        frames_in_flight_.advance();

        if (swapchain_suboptimal ||
            present_result == vk::Result::eSuboptimalKHR ||
            present_result == vk::Result::eErrorOutOfDateKHR) {
            throw vk::OutOfDateKHRError(
                "swapchain recreation required!"
            );
        }
    }

    auto wait_idle(const Device& device) const -> void {
        device.logical_device().waitIdle();
    }

    // Returns true when a replacement swapchain was created.
    auto recreate_swapchain(const DeviceContext& device_context, const Window& window) -> bool;

    auto render_pass() const noexcept -> const vk::raii::RenderPass& {
        return swapchain_.render_pass();
    }

    auto frame_count() const noexcept -> uint32_t {
        return frames_in_flight_.frame_count();
    }

private:
    Swapchain swapchain_;
    FramesInFlight frames_in_flight_;

    template <typename Recorder> requires std::invocable<Recorder&&, const RenderFrameContext&>
    auto record(uint32_t image_id, Recorder&& recorder) -> void {
        auto& frame = frames_in_flight_.current_frame();
        frame.record([&](vk::raii::CommandBuffer& command_buffer) {
            const RenderFrameContext context{
                .command_buffer = command_buffer,
                .render_pass = swapchain_.render_pass(),
                .framebuffer = swapchain_.framebuffers().at(image_id),
                .extent = swapchain_.extent(),
                .frame_index = frames_in_flight_.current_frame_index()
            };
            std::forward<Recorder>(recorder)(context);
        });
    }

    auto submit(const Device& device, uint32_t image_id) -> void;
    auto present(const Device& device, uint32_t image_id) -> vk::Result;
};

#endif
