#ifndef DEVICE_CONTEXT_HPP
#define DEVICE_CONTEXT_HPP

#include "platform/window.hpp"
#include "gfx/core/instance.hpp"
#include "gfx/core/surface.hpp"
#include "gfx/core/device.hpp"

class DeviceContext {
public:
    DeviceContext() = default;

    // create the vulkan context
    auto create(const Window& window) -> void {
        instance_.create();
        surface_.create(instance_, window);
        device_.create(instance_, surface_);
    }

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

private:
    Instance instance_;
    Surface surface_;
    Device device_;
};

#endif
