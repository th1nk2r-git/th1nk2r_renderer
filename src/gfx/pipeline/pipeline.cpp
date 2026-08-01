#include "gfx/pipeline/pipeline.hpp"

#include <utility>

Pipeline::Pipeline(vk::raii::Pipeline handle, vk::PipelineBindPoint bind_point) noexcept
    : handle_(std::move(handle)), bind_point_(bind_point) {}

auto Pipeline::bind(vk::raii::CommandBuffer& command_buffer) const -> void {
    command_buffer.bindPipeline(bind_point_, *handle_);
}
