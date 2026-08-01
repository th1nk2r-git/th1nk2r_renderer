#include "gfx/descriptor/descriptor_pool.hpp"

DescriptorPool::DescriptorPool(const Device& device, uint32_t max_sets, std::span<const vk::DescriptorPoolSize> pool_sizes) {
    vk::DescriptorPoolCreateInfo create_info{};

    create_info
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setMaxSets(max_sets)
        .setPoolSizes(pool_sizes);

    handle_ = device.logical_device().createDescriptorPool(create_info);
}

auto DescriptorPool::allocate_sets(
    const Device& device,
    std::span<const vk::DescriptorSetLayout> layouts
) -> std::vector<vk::raii::DescriptorSet> {
    if (layouts.empty()) {
        return {};
    }

    vk::DescriptorSetAllocateInfo allocate_info{};
    allocate_info
        .setDescriptorPool(*handle_)
        .setSetLayouts(layouts);

    return device.logical_device().allocateDescriptorSets(allocate_info);
}
