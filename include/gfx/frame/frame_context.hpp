#ifndef FRAME_CONTEXT_HPP
#define FRAME_CONTEXT_HPP

#include <cstdint>
#include <vector>

#include "gfx/frame/frame_resources.hpp"

class FrameContext {
public:
    FrameContext() = default;
    FrameContext(const Device& device);

    FrameContext(const FrameContext&) = delete;
    auto operator=(const FrameContext&) -> FrameContext& = delete;

    FrameContext(FrameContext&&) noexcept = default;
    auto operator=(FrameContext&&) noexcept -> FrameContext& = default;

    // return the current frame index
    auto current_frame_index() const noexcept -> uint32_t {
        return current_frame_index_;
    }

    // return the number of frames that may be in flight
    auto frame_count() const noexcept -> uint32_t {
        return static_cast<uint32_t>(frame_resources_.size());
    }

    // return the resources of the current frame in flight
    auto current_frame_resources() -> FrameResources& {
        return frame_resources_[current_frame_index_];
    }

    // return the resources of the current frame in flight
    auto current_frame_resources() const -> const FrameResources& {
        return frame_resources_[current_frame_index_];
    }

    // return the image_available semaphore of the current frame in flight
    auto current_image_available() const -> const vk::raii::Semaphore& {
        return current_frame_resources().image_available();
    }

    // return the fence of the current frame in flight
    auto current_in_flight_fence() const -> const vk::raii::Fence& {
        return current_frame_resources().in_flight_fence();
    }

    // return the command context of the current frame in flight
    auto current_command_context() -> CommandContext& {
        return current_frame_resources().command_context();
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
    uint32_t current_frame_index_ = 0;
    std::vector<FrameResources> frame_resources_;
};

#endif
