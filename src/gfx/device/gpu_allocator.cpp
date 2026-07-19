#define VMA_IMPLEMENTATION
#include "gfx/device/gpu_allocator.hpp"
#include "gfx/device/device.hpp"
#include "gfx/device/instance.hpp"

#include <stdexcept>
#include <string>

GpuAllocator::~GpuAllocator() noexcept {
    if (handle_ != nullptr) {
        vmaDestroyAllocator(handle_);
        handle_ = nullptr;
    }
}

auto GpuAllocator::create(const Instance& instance, const Device& device) -> void {
    if (handle_ != nullptr) {
        throw std::logic_error("GPU allocator is already created!");
    }

    VmaAllocatorCreateInfo create_info{};
    create_info.instance = static_cast<VkInstance>(*instance.get());
    create_info.physicalDevice = static_cast<VkPhysicalDevice>(*device.physical_device());
    create_info.device = static_cast<VkDevice>(*device.logical_device());
    create_info.vulkanApiVersion = VK_API_VERSION_1_4;

    VmaAllocator new_handle = nullptr;
    const VkResult result = vmaCreateAllocator(
        &create_info,
        &new_handle
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "failed to create GPU allocator, VkResult: " +
            std::to_string(static_cast<int>(result))
        );
    }

    handle_ = new_handle;
}
