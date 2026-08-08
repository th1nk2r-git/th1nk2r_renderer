#include "gfx/frame/frame_resources.hpp"
#include <array>
#include <utility>

FrameResources::FrameResources(const Device& device) 
    : image_available_(device.logical_device().createSemaphore(vk::SemaphoreCreateInfo{})),
    in_flight_fence_(
        device.logical_device().createFence(vk::FenceCreateInfo{
            .flags = vk::FenceCreateFlagBits::eSignaled
        })
    ),
    command_context_(device, device.graphics_family(), 1) {}