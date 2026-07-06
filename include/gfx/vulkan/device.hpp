#ifndef DEVICE_HPP
#define DEVICE_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "platform/window.hpp"
#include "gfx/vulkan/instance.hpp"
#include "gfx/vulkan/surface.hpp"

class Device {
public:
    Device() = default;

    ~Device() = default;

    // create the device
    auto create(const Instance& instance, const Surface& surface) -> void {
        pick_physical_device(instance, surface);
        create_logical_device(surface);
        create_graphics_queue();
        create_present_queue();
    }

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
        return graphics_family_.value();
    }

    // return the present family
    auto present_family() const -> uint32_t {
        return present_family_.value();
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
    vk::raii::PhysicalDevice physical_device_ = nullptr;
    vk::raii::Device logical_device_ = nullptr;

    std::optional<uint32_t> graphics_family_;
    std::optional<uint32_t> present_family_; 

    vk::raii::Queue graphics_queue_ = nullptr;
    vk::raii::Queue present_queue_ = nullptr;

    // check if the equipment is suitable
    auto is_device_suitable(const vk::raii::PhysicalDevice& dev, const Surface& surface) -> bool;

    // pick a suitable GPU
    auto pick_physical_device(const Instance& instance, const Surface& surface) -> void;

    // create the logical device
    auto create_logical_device(const Surface& surface) -> void;

    // create the graphics queue
    auto create_graphics_queue() -> void {
        graphics_queue_ = logical_device_.getQueue(graphics_family_.value(), 0);
    }

    // create the present queue
    auto create_present_queue() -> void {
        present_queue_ = logical_device_.getQueue(present_family_.value(), 0);
    }
};

#endif