#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan_raii.hpp>
#include <optional>

#include "window.hpp"

class VulkanContext {
public:
    VulkanContext() = default;

    // create the vulkan instance
    auto create_instance() -> void;

    // pick a suitable GPU
    auto pick_physical_device(const vk::raii::SurfaceKHR& surface) -> void;

    // create the logical device
    auto create_logical_device(const vk::raii::SurfaceKHR& surface) -> void;

    // create the graphics queue
    auto create_graphics_queue() -> void {
        graphics_queue = device.getQueue(graphics_family.value(), 0);
    }

    // create the present queue
    auto create_present_queue() -> void {
        present_queue = device.getQueue(present_family.value(), 0);
    }

    // return the reference of the vulkan instance
    auto get_instance() const -> const vk::raii::Instance& {
        return instance;
    }

    // return the const reference of the logical device
    auto get_device() const -> const vk::raii::Device& {
        return device;
    }

    // return the const reference of the physical device
    auto get_physical_device() const -> const vk::raii::PhysicalDevice& {
        return physical_device;
    }

    // return the graphics family
    auto get_graphics_family() const -> uint32_t {
        return graphics_family.value();
    }

    // return the present family
    auto get_present_family() const -> uint32_t {
        return present_family.value();
    }

private:

    vk::raii::Context dispatcher;

    vk::raii::Instance instance = nullptr;

#ifdef NDEBUG
    bool validation_layers_enabled = false;
#else 
    bool validation_layers_enabled = true;
#endif

    const std::vector<const char*> validation_layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    vk::raii::PhysicalDevice physical_device = nullptr;
    vk::raii::Device device = nullptr;

    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family; 

    vk::raii::Queue graphics_queue = nullptr;
    vk::raii::Queue present_queue = nullptr;


    auto check_validation_layer_support() -> bool;

    auto is_device_suitable(const vk::raii::PhysicalDevice& dev, const vk::raii::SurfaceKHR& surface) -> bool;
};

#endif