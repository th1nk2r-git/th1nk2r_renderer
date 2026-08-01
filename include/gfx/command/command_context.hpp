#ifndef COMMAND_CONTEXT_HPP
#define COMMAND_CONTEXT_HPP

#include <utility>

#include "gfx/device/device.hpp"

class CommandContext {
public:
    CommandContext(const Device& device, const uint32_t queue_family, const uint32_t buffer_count) : queue_family_(queue_family) {
        create_command_pool(device, queue_family);
        create_command_buffers(device, buffer_count);
    }

    // return the const reference of the command pool
    auto command_pool() const -> const vk::raii::CommandPool& {
        return command_pool_;
    }

    // return the reference of the command buffers
    auto command_buffers() -> std::vector<vk::raii::CommandBuffer>& {
        return command_buffers_;
    }

    // return the queue family of the command pool
    auto queue_family() -> uint32_t {
        return queue_family_;
    }

    // record on the specified command buffer
    template <typename Function>
    auto record(uint32_t buffer_index, Function&& execute) -> void {
        vk::CommandBufferBeginInfo begin_info{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        command_buffers_[buffer_index].begin(begin_info);
        execute(command_buffers_[buffer_index]);
        command_buffers_[buffer_index].end();
    }

    // record on the default command buffer
    template <typename Function>
    auto record(Function&& execute) -> void {
        record(0, std::forward<Function>(execute));
    }


private:
    vk::raii::CommandPool command_pool_ = nullptr;
    std::vector<vk::raii::CommandBuffer> command_buffers_;

    const uint32_t queue_family_;

    // create the command pool
    auto create_command_pool(const Device& device, const uint32_t queue_family) -> void;

    // create a specific number of command buffers
    auto create_command_buffers(const Device& device, const uint32_t buffer_count) -> void;
};

#endif
