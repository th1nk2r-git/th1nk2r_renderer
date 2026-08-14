#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <vulkan/vulkan_raii.hpp>

#include "gfx/device/memory_allocator.hpp"

enum class BufferMemoryUsage {
    GpuOnly,
    Upload,
    Readback
};

struct BufferDesc {
    vk::DeviceSize size = 0;
    vk::BufferUsageFlags usage{};
    BufferMemoryUsage memory = BufferMemoryUsage::GpuOnly;
    bool persistent_mapping = false;
};

class Buffer {
public:
    Buffer() = default;
    Buffer(const MemoryAllocator& allocator, const BufferDesc& desc);
    ~Buffer() noexcept;

    Buffer(const Buffer&) = delete;
    auto operator=(const Buffer&) -> Buffer& = delete;
    Buffer(Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

    auto get() const noexcept -> vk::Buffer {
        return vk::Buffer{handle_};
    }

    auto size() const noexcept -> vk::DeviceSize {
        return size_;
    }

    auto usage() const noexcept -> vk::BufferUsageFlags {
        return usage_;
    }

    auto write(const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) -> void;

private:
    VmaAllocator allocator_ = nullptr;
    VkBuffer handle_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;

    vk::DeviceSize size_ = 0;
    vk::BufferUsageFlags usage_{};
    void* mapped_data_ = nullptr;
};

#endif
