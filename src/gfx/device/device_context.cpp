#include "gfx/device/device_context.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
#ifdef NDEBUG
    constexpr bool validation_layers_enabled = false;
#else
    constexpr bool validation_layers_enabled = true;
#endif

    constexpr std::array validation_layers{
        "VK_LAYER_KHRONOS_validation"
    };

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
        vk::DebugUtilsMessageTypeFlagsEXT,
        const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
        void*
    ) {
        const bool is_error =
            message_severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        const char* severity = is_error ? "ERROR" : "WARNING";
        const char* color = is_error ? "\x1b[31m" : "\x1b[33m";

        std::cerr << color
                  << "[Vulkan][" << severity << "] "
                  << callback_data->pMessage
                  << "\x1b[0m\n";
        return VK_FALSE;
    }

    auto make_debug_messenger_create_info()
        -> vk::DebugUtilsMessengerCreateInfoEXT {
        return {
            .messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = debug_callback
        };
    }

    auto check_validation_layer_support() -> bool {
        const auto available_layers = vk::enumerateInstanceLayerProperties();
        for (const char* layer_name : validation_layers) {
            bool found = false;
            for (const auto& layer : available_layers) {
                if (std::strcmp(layer_name, layer.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    auto create_instance(
        vk::raii::Context& dispatcher,
        bool validation_enabled
    ) -> vk::raii::Instance {
        constexpr vk::ApplicationInfo app_info{
            .pApplicationName = "th1nk2r renderer",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "th1nk2r engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14
        };

        uint32_t glfw_extension_count = 0;
        const char** glfw_extensions =
            glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if (glfw_extensions == nullptr) {
            throw std::runtime_error(
                "failed to get required GLFW extensions!"
            );
        }

        std::vector<const char*> extensions(
            glfw_extensions,
            glfw_extensions + glfw_extension_count
        );
        auto debug_create_info = make_debug_messenger_create_info();

        if (validation_enabled) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        vk::InstanceCreateInfo create_info{
            .pApplicationInfo = &app_info,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };

        if (validation_enabled) {
            create_info.enabledLayerCount =
                static_cast<uint32_t>(validation_layers.size());
            create_info.ppEnabledLayerNames = validation_layers.data();
            create_info.pNext = &debug_create_info;
        }
        else if (validation_layers_enabled) {
            std::cerr << "[Vulkan][WARNING] Validation layer is unavailable; "
                         "debug validation is disabled.\n";
        }

        return vk::raii::Instance(dispatcher, create_info);
    }

    auto create_debug_messenger(
        const vk::raii::Instance& instance,
        bool validation_enabled
    ) -> vk::raii::DebugUtilsMessengerEXT {
        if (!validation_enabled) {
            return vk::raii::DebugUtilsMessengerEXT(nullptr);
        }
        return vk::raii::DebugUtilsMessengerEXT(
            instance,
            make_debug_messenger_create_info()
        );
    }

    auto create_surface(
        const vk::raii::Instance& instance,
        const Window& window
    ) -> vk::raii::SurfaceKHR {
        VkSurfaceKHR raw_handle = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(
                *instance,
                window.get(),
                nullptr,
                &raw_handle
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
        return vk::raii::SurfaceKHR(instance, raw_handle);
    }
}

DeviceContext::DeviceContext(const Window& window)
    : dispatcher_(),
      validation_enabled_(
          validation_layers_enabled && check_validation_layer_support()
      ),
      instance_(create_instance(dispatcher_, validation_enabled_)),
      debug_messenger_(
          create_debug_messenger(instance_, validation_enabled_)
      ),
      surface_(create_surface(instance_, window)),
      device_(instance_, surface_),
      allocator_(instance_, device_),
      buffer_uploader_(device_, allocator_),
      image_uploader_(device_, allocator_) {}
