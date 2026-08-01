#define VMA_IMPLEMENTATION
#include "gfx/device/gpu_allocator.hpp"
#include "gfx/device/device.hpp"
#include "gfx/device/instance.hpp"

#include <stdexcept>
#include <string>
#include <utility>

GpuAllocator::GpuAllocator(const Instance& instance, const Device& device) {
    VmaAllocatorCreateInfo create_info{};
    create_info.instance = static_cast<VkInstance>(*instance.get());
    create_info.physicalDevice = static_cast<VkPhysicalDevice>(*device.physical_device());
    create_info.device = static_cast<VkDevice>(*device.logical_device());
    create_info.vulkanApiVersion = VK_API_VERSION_1_4;

    const VkResult result = vmaCreateAllocator(
        &create_info,
        &handle_
    );

    if (result != VK_SUCCESS) {
        handle_ = nullptr;
        throw std::runtime_error(
            "failed to create GPU allocator, VkResult: " +
            std::to_string(static_cast<int>(result))
        );
    }
}

GpuAllocator::~GpuAllocator() noexcept {
    if (handle_ != nullptr) {
        vmaDestroyAllocator(handle_);
        handle_ = nullptr;
    }
}

GpuAllocator::GpuAllocator(GpuAllocator&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

auto GpuAllocator::operator=(GpuAllocator&& other) noexcept -> GpuAllocator& {
    if (this == &other) {
        return *this;
    }

    if (handle_ != nullptr) {
        vmaDestroyAllocator(handle_);
    }
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
}
