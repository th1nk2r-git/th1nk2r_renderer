#include "gfx/frame/frames_in_flight.hpp"

#include <limits>
#include <utility>

namespace {
    auto create_command_pool(const Device& device) -> vk::raii::CommandPool {
        const vk::CommandPoolCreateInfo create_info{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = device.graphics_family()
        };
        return device.logical_device().createCommandPool(create_info);
    }

    auto create_command_buffer(
        const Device& device,
        const vk::raii::CommandPool& command_pool
    ) -> vk::raii::CommandBuffer {
        const vk::CommandBufferAllocateInfo allocate_info{
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        auto command_buffers =
            device.logical_device().allocateCommandBuffers(allocate_info);
        return std::move(command_buffers.front());
    }

    auto create_frames(
        const Device& device,
        uint32_t frame_count
    ) -> std::vector<Frame> {
        std::vector<Frame> frames;
        frames.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            frames.emplace_back(device);
        }
        return frames;
    }
}

Frame::Frame(const Device& device)
    : command_pool(create_command_pool(device)),
      command_buffer(create_command_buffer(device, command_pool)),
      image_available(
          device.logical_device().createSemaphore(vk::SemaphoreCreateInfo{})
      ),
      in_flight_fence(
          device.logical_device().createFence(vk::FenceCreateInfo{
              .flags = vk::FenceCreateFlagBits::eSignaled
          })
      ) {}

FramesInFlight::FramesInFlight(const Device& device)
    : frames_(create_frames(device, max_frames_in_flight_)) {}

auto FramesInFlight::wait(const Device& device) -> void {
    static_cast<void>(device.logical_device().waitForFences(
        *current_frame().in_flight_fence,
        true,
        std::numeric_limits<uint64_t>::max()
    ));
}

auto FramesInFlight::reset(const Device& device) -> void {
    device.logical_device().resetFences(*current_frame().in_flight_fence);
}
