#ifndef DESCRIPTOR_SET_LAYOUT_HPP
#define DESCRIPTOR_SET_LAYOUT_HPP

#include <span>

#include "gfx/device/device.hpp"

class DescriptorSetLayout {
public:
    DescriptorSetLayout() = default;

    DescriptorSetLayout(const Device& device, std::span<const vk::DescriptorSetLayoutBinding> bindings);

    auto get() const -> const vk::raii::DescriptorSetLayout& {
        return handle_;
    }

private:
    vk::raii::DescriptorSetLayout handle_ = nullptr;
};

#endif