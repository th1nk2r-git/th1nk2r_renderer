#include "gfx/device/device_context.hpp"

auto DeviceContext::create(const Window& window) -> void {
    instance_.create();
    surface_.create(instance_, window);
    device_.create(instance_, surface_);
    allocator_.create(instance_, device_);
}