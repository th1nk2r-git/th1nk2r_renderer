#ifndef PIPELINE_STATE_HPP
#define PIPELINE_STATE_HPP

#include <cstdint>
#include <vector>

#include "gfx/descriptor/descriptor_set_layout.hpp"
#include "gfx/pipeline/pipeline.hpp"
#include "gfx/pipeline/pipeline_layout.hpp"

class PipelineState {
public:
    PipelineState() = default;
    PipelineState(
        std::vector<DescriptorSetLayout> descriptor_set_layouts,
        PipelineLayout layout,
        Pipeline pipeline
    ) noexcept;

    PipelineState(const PipelineState&) = delete;
    auto operator=(const PipelineState&) -> PipelineState& = delete;
    PipelineState(PipelineState&&) noexcept = default;
    auto operator=(PipelineState&& other) noexcept -> PipelineState&;

    // bind the pipeline to the command buffer
    auto bind(vk::raii::CommandBuffer& command_buffer) const -> void;

    // return the const reference of the pipeline
    auto pipeline() const noexcept -> const Pipeline& {
        return pipeline_;
    }

    // return the const reference of the pipeline layout
    auto layout() const noexcept -> const PipelineLayout& {
        return layout_;
    }

    // return the const reference of the descriptor set layout
    auto descriptor_set_layout(uint32_t set) const -> const DescriptorSetLayout&;

private:
    std::vector<DescriptorSetLayout> descriptor_set_layouts_;
    PipelineLayout layout_;
    Pipeline pipeline_;
};

#endif
