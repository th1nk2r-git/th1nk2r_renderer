#ifndef DESCRIPTOR_POOL_HPP
#define DESCRIPTOR_POOL_HPP

#include <span>
#include <vector>

#include "gfx/device/device.hpp"

class DescriptorPool {
public:
    DescriptorPool() = default;
    DescriptorPool(const Device& device, uint32_t max_sets, std::span<const vk::DescriptorPoolSize> pool_sizes);

    auto get() const -> const vk::raii::DescriptorPool& {
        return handle_;
    }

    auto allocate_sets(
        const Device& device, 
        std::span<const vk::DescriptorSetLayout> layouts
    ) -> std::vector<vk::raii::DescriptorSet>;

private:
    vk::raii::DescriptorPool handle_ = nullptr;
};

#endif
