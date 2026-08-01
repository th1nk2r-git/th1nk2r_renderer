#include "gfx/descriptor/descriptor_set_layout.hpp"

DescriptorSetLayout::DescriptorSetLayout(const Device& device, std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    vk::DescriptorSetLayoutCreateInfo create_info{};
    create_info.setBindings(bindings);
    handle_ = device.logical_device().createDescriptorSetLayout(create_info);
}