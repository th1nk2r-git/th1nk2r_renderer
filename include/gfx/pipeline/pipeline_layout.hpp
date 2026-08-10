#ifndef PIPELINE_LAYOUT_HPP
#define PIPELINE_LAYOUT_HPP

#include <span>

#include "gfx/device/device.hpp"

class PipelineLayout {
public:
    PipelineLayout() = default;

    PipelineLayout(
        const Device& device,
        std::span<const vk::DescriptorSetLayout> set_layouts,
        std::span<const vk::PushConstantRange> push_constant_ranges = {}
    );

    auto get() const -> const vk::raii::PipelineLayout& {
        return handle_;
    }

private:
    vk::raii::PipelineLayout handle_ = nullptr;
};

#endif
