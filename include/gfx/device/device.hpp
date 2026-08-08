#ifndef DEVICE_HPP
#define DEVICE_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <optional>

#include "gfx/device/instance.hpp"
#include "gfx/device/surface.hpp"
#include "platform/window.hpp"

class Device {
public:
    Device(const Instance& instance, const Surface& surface);
    ~Device() = default;

    Device(const Device&) = delete;
    auto operator=(const Device&) -> Device& = delete;
    Device(Device&&) = delete;
    auto operator=(Device&&) -> Device& = delete;

    // return the const reference of the logical device
    auto logical_device() const -> const vk::raii::Device& {
        return logical_device_;
    }

    // return the const reference of the physical device
    auto physical_device() const -> const vk::raii::PhysicalDevice& {
        return physical_device_;
    }

    // return the graphics family
    auto graphics_family() const -> uint32_t {
        return graphics_family_;
    }

    // return the present family
    auto present_family() const -> uint32_t {
        return present_family_;
    }

    // return the present family
    auto graphics_queue() const -> const vk::raii::Queue& {
        return graphics_queue_;
    }

    // return the present family
    auto present_queue() const -> const vk::raii::Queue& {
        return present_queue_;
    }

private:
    struct QueueFamilies {
        uint32_t graphics;
        uint32_t present;
    };

    struct SelectedPhysicalDevice {
        vk::raii::PhysicalDevice physical_device;
        QueueFamilies queue_families;
    };

    explicit Device(SelectedPhysicalDevice selected);

    vk::raii::PhysicalDevice physical_device_ = nullptr;
    uint32_t graphics_family_ = 0;
    uint32_t present_family_ = 0;
    vk::raii::Device logical_device_ = nullptr;

    vk::raii::Queue graphics_queue_ = nullptr;
    vk::raii::Queue present_queue_ = nullptr;

    static auto select_physical_device(
        const Instance& instance,
        const Surface& surface
    ) -> SelectedPhysicalDevice;

    static auto find_queue_families(
        const vk::raii::PhysicalDevice& physical_device,
        const Surface& surface
    ) -> std::optional<QueueFamilies>;

    static auto create_logical_device(
        const vk::raii::PhysicalDevice& physical_device,
        QueueFamilies queue_families
    ) -> vk::raii::Device;
};

#endif
