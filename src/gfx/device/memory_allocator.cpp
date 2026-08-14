#define VMA_IMPLEMENTATION
#include "gfx/device/memory_allocator.hpp"
#include "gfx/device/device.hpp"

#include <stdexcept>
#include <string>

namespace {
auto create_allocator(
    const vk::raii::Instance& instance,
    const Device& device
) -> VmaAllocator {
    VmaAllocatorCreateInfo create_info{};
    create_info.instance = static_cast<VkInstance>(*instance);
    create_info.physicalDevice = static_cast<VkPhysicalDevice>(*device.physical_device());
    create_info.device = static_cast<VkDevice>(*device.logical_device());
    create_info.vulkanApiVersion = VK_API_VERSION_1_4;

    VmaAllocator handle = nullptr;
    const VkResult result = vmaCreateAllocator(
        &create_info,
        &handle
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "failed to create GPU allocator, VkResult: " +
            std::to_string(static_cast<int>(result))
        );
    }

    return handle;
}
}

MemoryAllocator::MemoryAllocator(
    const vk::raii::Instance& instance,
    const Device& device
)
    : handle_(create_allocator(instance, device)) {}

MemoryAllocator::~MemoryAllocator() noexcept {
    if (handle_ != nullptr) {
        vmaDestroyAllocator(handle_);
        handle_ = nullptr;
    }
}
