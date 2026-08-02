#ifndef FRAME_RESOURCES_HPP
#define FRAME_RESOURCES_HPP

#include "gfx/command/command_context.hpp"
#include "gfx/descriptor/descriptor_pool.hpp"
#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/device/device.hpp"
#include "gfx/resource/buffer.hpp"

class FrameResources {
public:
    FrameResources(const Device& device);

    FrameResources(const FrameResources&) = delete;
    auto operator=(const FrameResources&) -> FrameResources& = delete;

    FrameResources(FrameResources&&) noexcept = default;
    auto operator=(FrameResources&&) noexcept -> FrameResources& = delete;

    // return the image_available semaphore
    auto image_available() const -> const vk::raii::Semaphore& {
        return image_available_;
    }

    // return the in-flight fence
    auto in_flight_fence() const -> const vk::raii::Fence& {
        return in_flight_fence_;
    }

    // return the command context
    auto command_context() -> CommandContext& {
        return command_context_;
    }

private:
    vk::raii::Semaphore image_available_ = nullptr;
    vk::raii::Fence in_flight_fence_ = nullptr;
    CommandContext command_context_;
};

#endif
