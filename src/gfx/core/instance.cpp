#include <GLFW/glfw3.h>
#include "gfx/core/instance.hpp"


auto Instance::create() -> void {
    constexpr vk::ApplicationInfo app_info {
        .pApplicationName   = "th1nk2r renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "th1nk2r engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = vk::ApiVersion14
    };

    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions) {
        throw std::runtime_error("failed to get required glfw extensions!");
    }

    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    vk::InstanceCreateInfo create_info{
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    if (validation_layers_enabled && check_validation_layer_support()) {
        create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
    }

    this->handle_ = vk::raii::Instance(dispatcher, create_info);
}

auto Instance::check_validation_layer_support() -> bool {
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
