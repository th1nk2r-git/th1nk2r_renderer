#include "vulkan_context.hpp"

#include <optional>
#include <set>

auto VulkanContext::create_instance() -> void {
    constexpr vk::ApplicationInfo app_info {
        .pApplicationName   = "th1nk2r renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "no engine",
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

    this->instance = vk::raii::Instance(dispatcher, create_info);
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
    throw std::runtime_error("failed to find a suitable GPU!");
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

    auto available_formats = dev.getSurfaceFormatsKHR(surface);
    auto available_present_mode = dev.getSurfacePresentModesKHR(surface);

    if (available_formats.empty() || available_present_mode.empty()) {
        return false;
    }
    
    auto available_extensions = dev.enumerateDeviceExtensionProperties();
    std::set<std::string> required_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    for (const auto& extension : available_extensions) {
        required_extensions.erase(extension.extensionName);
    }
    
    return required_extensions.empty();
}

auto VulkanContext::create_logical_device(const vk::raii::SurfaceKHR& surface) -> void {
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
    feature_chain = {
        {},                                    // vk::PhysicalDeviceFeatures2 (empty for now)
        {.shaderDrawParameters = true},        // Enable shader draw parameters from Vulkan 1.1
        {.dynamicRendering = true},            // Enable dynamic rendering from Vulkan 1.3
        {.extendedDynamicState = true}         // Enable extended dynamic state from the extension
    };

    auto queue_families = physical_device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            this->graphics_family = i;
        }
        vk::Bool32 present_support = physical_device.getSurfaceSupportKHR(i, surface);
        if (present_support) {
            this->present_family = i;
        }
        if (this->graphics_family.has_value() && this->present_family.has_value()) {
            break;
        }
    }

    std::set<uint32_t> unique_queue_families = {
        this->graphics_family.value(),
        this->present_family.value()
    };

    float queue_priority = 0.5f;
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    for (uint32_t family : unique_queue_families) {
        queue_create_infos.push_back({
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority
        });
    }

    std::vector<const char*> required_device_extensions = {
        vk::KHRSwapchainExtensionName
    };

    vk::DeviceCreateInfo device_create_info {
        .pNext = &feature_chain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(required_device_extensions.size()),
        .ppEnabledExtensionNames = required_device_extensions.data()
    };

    this->device = physical_device.createDevice(device_create_info);
}