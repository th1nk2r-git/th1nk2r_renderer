#include "vulkan_context.hpp"

#include <optional>
#include <set>

auto VulkanContext::create_instance() -> void {
    constexpr vk::ApplicationInfo app_info {
        .pApplicationName   = "th1nk2r renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = vk::ApiVersion14
    };

    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions) {
        throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
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

    instance = vk::raii::Instance(dispatcher, create_info);
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


auto VulkanContext::pick_physical_device(const vk::raii::SurfaceKHR& surface) -> void {
    auto physical_devices = instance.enumeratePhysicalDevices();
    for (auto& dev : physical_devices) {
        if (is_device_suitable(dev, surface)) {
            physical_device = dev;
            return;
        }
    }
    throw std::runtime_error("failed to find a suitable GPU");
}


auto VulkanContext::is_device_suitable(const vk::raii::PhysicalDevice& dev, const vk::raii::SurfaceKHR& surface) -> bool {
    auto queue_families = dev.getQueueFamilyProperties();

    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;
    
    for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_family = i;
        }
        vk::Bool32 present_support = dev.getSurfaceSupportKHR(i, surface);
        if (present_support) {
            present_family = i;
        }
        if (graphics_family.has_value() && present_family.has_value()) {
            break;
        }
    }
    
    if (!graphics_family.has_value() || !present_family.has_value()) {
        return false;
    }
    
    auto available_extensions = dev.enumerateDeviceExtensionProperties();
    std::set<std::string> required_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    for (const auto& extension : available_extensions) {
        required_extensions.erase(extension.extensionName);
    }
    
    return required_extensions.empty();
}