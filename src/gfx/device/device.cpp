#include <optional>
#include <set>

#include "gfx/device/device.hpp"

auto Device::pick_physical_device(const Instance& instance, const Surface& surface) -> void {
    auto physical_devices = instance.get().enumeratePhysicalDevices();
    for (auto& dev : physical_devices) {
        if (is_device_suitable(dev, surface)) {
            physical_device_ = dev;
            return;
        }
    }
    throw std::runtime_error("failed to find a suitable GPU!");
}


auto Device::is_device_suitable(const vk::raii::PhysicalDevice& dev, const Surface& surface) -> bool {
    auto queue_families = dev.getQueueFamilyProperties();
    
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_family = i;
        }
        vk::Bool32 present_support = dev.getSurfaceSupportKHR(i, surface.get());
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

    auto available_formats = dev.getSurfaceFormatsKHR(surface.get());
    auto available_present_mode = dev.getSurfacePresentModesKHR(surface.get());

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

auto Device::create_logical_device(const Surface& surface) -> void {
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
    feature_chain = {
        {},                                    // vk::PhysicalDeviceFeatures2 (empty for now)
        {.shaderDrawParameters = true},        // Enable shader draw parameters from Vulkan 1.1
        {.dynamicRendering = false},            // Enable dynamic rendering from Vulkan 1.3
        {.extendedDynamicState = false}         // Enable extended dynamic state from the extension
    };

    auto queue_families = physical_device_.getQueueFamilyProperties();

    for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            this->graphics_family_ = i;
        }
        vk::Bool32 present_support = physical_device_.getSurfaceSupportKHR(i, surface.get());
        if (present_support) {
            this->present_family_ = i;
        }
        if (this->graphics_family_.has_value() && this->present_family_.has_value()) {
            break;
        }
    }

    std::set<uint32_t> unique_queue_families = {
        this->graphics_family_.value(),
        this->present_family_.value()
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

    this->logical_device_ = physical_device_.createDevice(device_create_info);
}
