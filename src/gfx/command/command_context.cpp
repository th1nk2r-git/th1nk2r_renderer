#include "gfx/command/command_context.hpp"

namespace {
    auto create_command_pool(
        const Device& device, 
        uint32_t queue_family
    ) -> vk::raii::CommandPool {
        const vk::CommandPoolCreateInfo create_info{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queue_family
        };
        return device.logical_device().createCommandPool(create_info);
    }

    auto create_command_buffers(
        const Device& device,
        const vk::raii::CommandPool& command_pool,
        uint32_t buffer_count
    ) -> std::vector<vk::raii::CommandBuffer> {
        const vk::CommandBufferAllocateInfo create_info{
            .commandPool = command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = buffer_count
        };
        return device.logical_device().allocateCommandBuffers(create_info);
    }
}

CommandContext::CommandContext(
    const Device& device,
    uint32_t queue_family,
    uint32_t buffer_count
) : command_pool_(
        create_command_pool(device, queue_family)
    ),
    command_buffers_(
        create_command_buffers(
            device,
            command_pool_,
            buffer_count
        )
    ),
    queue_family_(queue_family) {}
