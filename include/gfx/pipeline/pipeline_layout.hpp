#ifndef PIPELINE_LAYOUT_HPP
#define PIPELINE_LAYOUT_HPP

#include "gfx/device/device.hpp"

class PipelineLayout {
public:
    PipelineLayout() = default;

    PipelineLayout(const Device& device);

    auto get() const -> const vk::raii::PipelineLayout& {
        return handle_;
    }

private:
    vk::raii::PipelineLayout handle_ = nullptr;
};

#endif
