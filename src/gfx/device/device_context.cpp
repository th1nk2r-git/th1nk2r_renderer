#include "gfx/device/device_context.hpp"

#include <memory>
#include <utility>

DeviceContext::DeviceContext(const Window& window) {
    instance_ = Instance::make();
    surface_ = Surface(instance_, window);
    device_ = Device(instance_, surface_);
    allocator_ = GpuAllocator(instance_, device_);
}

DeviceContext::DeviceContext(DeviceContext&& other) noexcept
    : instance_(std::move(other.instance_)),
      surface_(std::move(other.surface_)),
      device_(std::move(other.device_)),
      allocator_(std::move(other.allocator_)) {}

auto DeviceContext::operator=(DeviceContext&& other) noexcept -> DeviceContext& {
    if (this == &other) {
        return *this;
    }

    std::destroy_at(this);
    std::construct_at(this, std::move(other));
    return *this;
}
