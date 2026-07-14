#ifndef FRAME_CONTEXT_HPP
#define FRAME_CONTEXT_HPP

#include <vector>
#include <array>

#include "gfx/core/device_context.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "gfx/command/command_context.hpp"

class FrameContext {
public:
    auto create(const DeviceContext& device_context, const SwapchainContext& swapchain_context) -> void;

    // recreate resources indexed by swapchain image.
    auto recreate_swapchain_resources(
        const Device& device,
        const SwapchainContext& swapchain_context
    ) -> void;

    // return the image_available semaphore of the current frame in flight
    auto current_image_available() const -> const vk::raii::Semaphore& {
        return image_available_[current_frame_index_];
    }

    // return the fence of the current frame in flight
    auto current_in_flight_fence() const -> const vk::raii::Fence& {
        return in_flight_fence_[current_frame_index_];
    }

    // return the render_finished semaphore of the image
    auto render_finished(uint32_t image_index) const -> const vk::raii::Semaphore& {
        return render_finished_[image_index];
    }

    // return the command context of the current frame in flight
    auto current_command_context() -> CommandContext& {
        return command_contexts_[current_frame_index_];
    }

    // wait for the fence of the current frame
    auto wait(const Device& device) -> void;

    // reset the fence
    auto reset(const Device& device) -> void;

    // move to the next frame
    auto advance() -> void {
        current_frame_index_ = (current_frame_index_ + 1) % max_frames_in_flight_;
    }

private:
    static constexpr uint32_t max_frames_in_flight_ = 2;
    uint32_t images_count = 0;

    uint32_t current_frame_index_ = 0;

    std::vector<CommandContext> command_contexts_;

    std::vector<vk::raii::Semaphore> render_finished_;
    std::vector<vk::raii::Semaphore> image_available_;
    std::vector<vk::raii::Fence> in_flight_fence_;

    // create the sync objects
    auto create_sync_objects(const Device& device) -> void;

    // create semaphores indexed by swapchain image
    auto create_render_finished_semaphores(const Device& device) -> void;

    // create the command contexts
    auto create_command_contexts(const Device& device) -> void;
};

#endif
