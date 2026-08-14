#ifndef DEVICE_CONTEXT_HPP
#define DEVICE_CONTEXT_HPP

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/device/buffer_uploader.hpp"
#include "gfx/device/image_uploader.hpp"
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
    auto instance() const -> const vk::raii::Instance& {
        return instance_;
    }

    // return the reference of the surface
    auto surface() const -> const vk::raii::SurfaceKHR& {
        return surface_;
    }

    // return the reference of the device
    auto device() const -> const Device& {
        return device_;
    }

    // return the reference of the allocator
    auto allocator() const -> const MemoryAllocator& {
        return allocator_;
    }

    auto buffer_uploader() noexcept -> BufferUploader& {
        return buffer_uploader_;
    }

    auto image_uploader() noexcept -> ImageUploader& {
        return image_uploader_;
    }

private:
    vk::raii::Context dispatcher_;
    bool validation_enabled_ = false;
    vk::raii::Instance instance_ = nullptr;
    vk::raii::DebugUtilsMessengerEXT debug_messenger_ = nullptr;
    vk::raii::SurfaceKHR surface_ = nullptr;
    Device device_;
    MemoryAllocator allocator_;
    BufferUploader buffer_uploader_;
    ImageUploader image_uploader_;
};

#endif
