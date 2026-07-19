#ifndef DEVICE_CONTEXT_HPP
#define DEVICE_CONTEXT_HPP

#include "platform/window.hpp"
#include "gfx/device/instance.hpp"
#include "gfx/device/surface.hpp"
#include "gfx/device/device.hpp"
#include "gfx/device/gpu_allocator.hpp"

class DeviceContext {
public:
    DeviceContext() = default;

    // create the vulkan context
    auto create(const Window& window) -> void;

    // return the reference of the instance
    auto instance() const -> const Instance& {
        return instance_;
    }
    
    // return the reference of the surface
    auto surface() const -> const Surface& {
        return surface_;
    }

    // return the reference of the device
    auto device() const -> const Device& {
        return device_;
    }

    // return the reference of the allocator
    auto allocator() const -> const GpuAllocator& {
        return allocator_;
    }

private:
    Instance instance_;
    Surface surface_;
    Device device_;
    GpuAllocator allocator_;
};

#endif
