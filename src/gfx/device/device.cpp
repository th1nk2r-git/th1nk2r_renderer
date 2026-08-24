#include "gfx/device/device.hpp"

#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

Device::Device(
    const vk::raii::Instance& instance,
    const vk::raii::SurfaceKHR& surface
)
    : Device(select_physical_device(instance, surface)) {}

Device::Device(SelectedPhysicalDevice selected)
    : physical_device_(std::move(selected.physical_device)),
      graphics_family_(selected.queue_families.graphics),
      present_family_(selected.queue_families.present),
      logical_device_(create_logical_device(physical_device_, selected.queue_families)),
      graphics_queue_(logical_device_.getQueue(graphics_family_, 0)),
      present_queue_(logical_device_.getQueue(present_family_, 0)) {}

auto Device::select_physical_device(
    const vk::raii::Instance& instance,
    const vk::raii::SurfaceKHR& surface
) -> SelectedPhysicalDevice {
    auto physical_devices = instance.enumeratePhysicalDevices();

    for (auto& physical_device : physical_devices) {
        if (physical_device.getProperties().apiVersion < vk::ApiVersion14) {
            continue;
        }

        const auto queue_families = find_queue_families(
            physical_device,
            surface
        );
        if (!queue_families.has_value()) {
            continue;
        }

        const auto available_formats = physical_device.getSurfaceFormatsKHR(*surface);
        const auto available_present_modes = physical_device.getSurfacePresentModesKHR(*surface);
        if (available_formats.empty() || available_present_modes.empty()) {
            continue;
        }
        if (!physical_device.getFeatures().imageCubeArray) {
            continue;
        }
        auto required_extensions = std::set<std::string>{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        for (const auto& extension : physical_device.enumerateDeviceExtensionProperties()) {
            required_extensions.erase(extension.extensionName);
        }
        if (!required_extensions.empty()) {
            continue;
        }

        return SelectedPhysicalDevice {
            .physical_device = std::move(physical_device),
            .queue_families = *queue_families
        };
    }

    throw std::runtime_error("failed to find a suitable GPU!");
}

auto Device::find_queue_families(
    const vk::raii::PhysicalDevice& physical_device,
    const vk::raii::SurfaceKHR& surface
) -> std::optional<QueueFamilies> {
    const auto properties = physical_device.getQueueFamilyProperties();
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    for (uint32_t index = 0; index < static_cast<uint32_t>(properties.size()); ++index) {
        const auto required_graphics_flags =
            vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
        if ((properties[index].queueFlags & required_graphics_flags) ==
            required_graphics_flags) {
            graphics_family = index;
        }
        if (physical_device.getSurfaceSupportKHR(index, *surface)) {
            present_family = index;
        }
        if (graphics_family.has_value() && present_family.has_value()) {
            return QueueFamilies{
                .graphics = *graphics_family,
                .present = *present_family
            };
        }
    }

    return std::nullopt;
}

auto Device::create_logical_device(
    const vk::raii::PhysicalDevice& physical_device,
    QueueFamilies queue_families
) -> vk::raii::Device {
    const std::set<uint32_t> unique_queue_families{
        queue_families.graphics,
        queue_families.present
    };

    constexpr float queue_priority = 0.5F;
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(unique_queue_families.size());
    for (const uint32_t family : unique_queue_families) {
        queue_create_infos.push_back({
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority
        });
    }

    const std::vector<const char*> required_device_extensions {
        vk::KHRSwapchainExtensionName
    };

    vk::PhysicalDeviceFeatures required_features{};
    required_features.setImageCubeArray(true);

    const vk::DeviceCreateInfo create_info{
        .queueCreateInfoCount =
            static_cast<uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledExtensionCount =
            static_cast<uint32_t>(required_device_extensions.size()),
        .ppEnabledExtensionNames = required_device_extensions.data(),
        .pEnabledFeatures = &required_features
    };

    return physical_device.createDevice(create_info);
}
