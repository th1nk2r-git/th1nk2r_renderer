#include "gfx/command/command_context.hpp"

auto CommandContext::create_command_pool(const Device& device, const uint32_t queue_family) -> void {
    vk::CommandPoolCreateInfo create_info;
    create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    create_info.queueFamilyIndex = queue_family;
    this->command_pool_ = device.logical_device().createCommandPool(create_info);
}

auto CommandContext::create_command_buffers(const Device& device, const uint32_t buffer_count) -> void {
    vk::CommandBufferAllocateInfo create_info;
    create_info.commandPool = command_pool_;
    create_info.level = vk::CommandBufferLevel::ePrimary;
    create_info.commandBufferCount = buffer_count;

    command_buffers_ = device.logical_device().allocateCommandBuffers(create_info);
}