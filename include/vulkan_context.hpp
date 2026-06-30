#ifndef VULKAN_CONTEXT
#define VULKAN_CONTEXT

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan_raii.hpp>

class VulkanContext {
public:
    VulkanContext() = default;

    // create the vulkan instance
    auto create_instance() -> void;

    // pick a suitable GPU
    auto pick_physical_device() -> void;

    // create the logical device
    auto create_logical_device() -> void;

    // create the graphics queue
    auto create_graphics_queue() -> void;

    // create the present queue
    auto create_present_queue() -> void;

    // return the reference of the vulkan instance
    auto get_instance() -> vk::raii::Instance& {
        return instance;
    }

    // return the reference of the logical device
    auto get_device() -> vk::raii::Device& {
        return device;
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
    vk::raii::Queue graphics_queue = nullptr;
    vk::raii::Queue present_queue = nullptr;

    auto check_validation_layer_support() -> bool;
};

#endif