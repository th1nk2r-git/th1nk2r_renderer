#include "vulkan_context.hpp"

auto VulkanContext::create_instance() -> void {
    constexpr vk::ApplicationInfo appInfo {
        .pApplicationName   = "th1nk2r renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = vk::ApiVersion14
    };

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo
    };

    instance = vk::raii::Instance(dispatcher, createInfo);
}

auto VulkanContext::check_validation_layer_support() -> bool {
    auto available_layers = vk::enumerateInstanceLayerProperties();
    for (const char* layer_name : validation_layers) {
        bool found = false;
        for (const auto& layer : available_layers) {
            if (strcmp(layer_name, layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;    
}
