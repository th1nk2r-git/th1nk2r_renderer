#ifndef GPU_ALLOCATOR_HPP
#define GPU_ALLOCATOR_HPP

#include <vk_mem_alloc.h>

class Instance;
class Device;

class GpuAllocator {
public:
    GpuAllocator() = default;
    GpuAllocator(const Instance& instance, const Device& device);
    ~GpuAllocator() noexcept;

    GpuAllocator(const GpuAllocator&) = delete;
    auto operator=(const GpuAllocator&) -> GpuAllocator& = delete;

    GpuAllocator(GpuAllocator&& other) noexcept;
    auto operator=(GpuAllocator&& other) noexcept -> GpuAllocator&;

    // return the handle
    auto get() const noexcept -> VmaAllocator {
        return handle_;
    }

private:
    VmaAllocator handle_ = nullptr;
};

#endif
