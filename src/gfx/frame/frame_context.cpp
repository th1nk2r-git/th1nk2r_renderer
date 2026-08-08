#include "gfx/frame/frame_context.hpp"

namespace {
    auto create_frame_resources(const Device& device, uint32_t frame_count) 
    -> std::vector<FrameResources> {
        std::vector<FrameResources> resources;
        resources.reserve(frame_count);
        for (uint32_t i = 0; i < frame_count; ++i) {
            resources.emplace_back(device);
        }
        return resources;
    }
}

FrameContext::FrameContext(const Device& device)
    : frame_resources_(create_frame_resources(device, max_frames_in_flight_)) {}

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
