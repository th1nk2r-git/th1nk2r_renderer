#include "gfx/descriptor/descriptor_set_layout.hpp"

namespace {
    auto create_descriptor_set_layout(
        const Device& device,
        std::span<const vk::DescriptorSetLayoutBinding> bindings) -> vk::raii::DescriptorSetLayout {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }
}

DescriptorSetLayout::DescriptorSetLayout(
    const Device& device,
    std::span<const vk::DescriptorSetLayoutBinding> bindings
) : handle_(create_descriptor_set_layout(device, bindings)) {}
