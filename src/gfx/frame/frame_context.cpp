#include "gfx/frame/frame_context.hpp"

FrameContext::FrameContext(const Device& device) {
    frame_resources_.reserve(max_frames_in_flight_);

    for (uint32_t i = 0; i < max_frames_in_flight_; ++i) {
        frame_resources_.emplace_back(device);
    }
}

auto FrameContext::wait(const Device& device) -> void {
    static_cast<void>(device.logical_device().waitForFences(
        *current_in_flight_fence(),
        true,
        std::numeric_limits<uint64_t>::max()
    ));
}

auto FrameContext::reset(const Device& device) -> void {
    device.logical_device().resetFences(
        *current_in_flight_fence()
    );
}
