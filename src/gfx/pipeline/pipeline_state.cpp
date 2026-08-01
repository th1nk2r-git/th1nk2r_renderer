#include "gfx/pipeline/pipeline_state.hpp"

#include <utility>

PipelineState::PipelineState(
    std::vector<DescriptorSetLayout> descriptor_set_layouts,
    PipelineLayout layout,
    Pipeline pipeline
) noexcept
    : descriptor_set_layouts_(std::move(descriptor_set_layouts)),
      layout_(std::move(layout)),
      pipeline_(std::move(pipeline)) {}

auto PipelineState::operator=(PipelineState&& other) noexcept -> PipelineState& {
    if (this == &other) {
        return *this;
    }

    pipeline_ = std::move(other.pipeline_);
    layout_ = std::move(other.layout_);
    descriptor_set_layouts_ = std::move(other.descriptor_set_layouts_);
    return *this;
}

auto PipelineState::bind(vk::raii::CommandBuffer& command_buffer) const -> void {
    pipeline_.bind(command_buffer);
}

auto PipelineState::descriptor_set_layout(uint32_t set) const -> const DescriptorSetLayout& {
    return descriptor_set_layouts_.at(set);
}
