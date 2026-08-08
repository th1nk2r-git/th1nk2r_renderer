#ifndef DEVICE_CONTEXT_HPP
#define DEVICE_CONTEXT_HPP

#include "gfx/device/data_uploader.hpp"
#include "gfx/device/device.hpp"
#include "gfx/device/gpu_allocator.hpp"
#include "gfx/device/instance.hpp"
#include "gfx/device/surface.hpp"
#include "platform/window.hpp"

class DeviceContext {
public:
    explicit DeviceContext(const Window& window);
    ~DeviceContext() = default;

    DeviceContext(const DeviceContext&) = delete;
    auto operator=(const DeviceContext&) -> DeviceContext& = delete;
    DeviceContext(DeviceContext&&) = delete;
    auto operator=(DeviceContext&&) -> DeviceContext& = delete;

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

    auto uploader() noexcept -> DataUploader& {
        return uploader_;
    }

private:
    Instance instance_;
    Surface surface_;
    Device device_;
    GpuAllocator allocator_;
    DataUploader uploader_;
};

#endif
