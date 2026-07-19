#include "gfx/device/instance.hpp"
#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
        vk::DebugUtilsMessageTypeFlagsEXT,
        const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
        void*
    ) {
        const bool is_error = message_severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        const char* severity = is_error ? "ERROR" : "WARNING";
        const char* color = is_error ? "\x1b[31m" : "\x1b[33m";

        std::cerr << color
                  << "[Vulkan][" << severity << "] "
                  << callback_data->pMessage
                  << "\x1b[0m\n";
        return VK_FALSE;
    }

    auto make_debug_messenger_create_info() -> vk::DebugUtilsMessengerCreateInfoEXT {
        return {
            .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                               vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = debug_callback
        };
    }
}

auto Instance::create() -> void {
    constexpr vk::ApplicationInfo app_info{
        .pApplicationName = "th1nk2r renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "th1nk2r engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14};

    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions) {
        throw std::runtime_error("failed to get required glfw extensions!");
    }

    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    const bool validation_enabled =
        validation_layers_enabled && check_validation_layer_support();
    auto debug_create_info = make_debug_messenger_create_info();

    if (validation_enabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo create_info{
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()};

    if (validation_enabled) {
        create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
        create_info.pNext = &debug_create_info;
    } else if (validation_layers_enabled) {
        std::cerr << "[Vulkan][WARNING] Validation layer is unavailable; "
                     "debug validation is disabled.\n";
    }

    this->handle_ = vk::raii::Instance(dispatcher, create_info);

    if (validation_enabled) {
        debug_messenger_ = vk::raii::DebugUtilsMessengerEXT(
            handle_,
            debug_create_info);
    }
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
        if (!found)
            return false;
    }
    return true;
}
