#ifndef GPU_ALLOCATOR_HPP
#define GPU_ALLOCATOR_HPP

#include <vk_mem_alloc.h>

class Instance;
class Device;

class GpuAllocator {
public:
    GpuAllocator() = default;
    ~GpuAllocator() noexcept;

    GpuAllocator(const GpuAllocator&) = delete;
    auto operator=(const GpuAllocator&) -> GpuAllocator& = delete;

    GpuAllocator(GpuAllocator&&) = delete;
    auto operator=(GpuAllocator&&) -> GpuAllocator& = delete;

    // create the gpu allocator
    auto create(const Instance& instance, const Device& device) -> void;

    // return the handle
    auto get() const noexcept -> VmaAllocator {
        return handle_;
    }

private:
    VmaAllocator handle_ = nullptr;
};

#endif
