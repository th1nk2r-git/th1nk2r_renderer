#ifndef MEMORY_ALLOCATOR_HPP
#define MEMORY_ALLOCATOR_HPP

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

class Device;

class MemoryAllocator {
public:
    MemoryAllocator(const vk::raii::Instance& instance, const Device& device);
    ~MemoryAllocator() noexcept;

    MemoryAllocator(const MemoryAllocator&) = delete;
    auto operator=(const MemoryAllocator&) -> MemoryAllocator& = delete;
    MemoryAllocator(MemoryAllocator&&) = delete;
    auto operator=(MemoryAllocator&&) -> MemoryAllocator& = delete;

    // return the handle
    auto get() const noexcept -> VmaAllocator {
        return handle_;
    }

private:
    VmaAllocator handle_ = nullptr;
};

#endif
