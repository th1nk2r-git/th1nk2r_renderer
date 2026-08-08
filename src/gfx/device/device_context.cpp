#include "gfx/device/device_context.hpp"

DeviceContext::DeviceContext(const Window& window)
    : instance_(),
      surface_(instance_, window),
      device_(instance_, surface_),
      allocator_(instance_, device_),
      uploader_(device_, allocator_) {}
