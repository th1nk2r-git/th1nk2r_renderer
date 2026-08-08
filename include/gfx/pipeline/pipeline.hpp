#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <vulkan/vulkan_raii.hpp>

class Pipeline {
public:
    Pipeline() = default;
    Pipeline(vk::raii::Pipeline handle, vk::PipelineBindPoint bind_point) noexcept;

    Pipeline(const Pipeline&) = delete;
    auto operator=(const Pipeline&) -> Pipeline& = delete;
    Pipeline(Pipeline&&) noexcept = default;
    auto operator=(Pipeline&&) noexcept -> Pipeline& = default;

    // bind the pipeline using the bind point fixed at creation time
    auto bind(vk::raii::CommandBuffer& command_buffer) const -> void;

    // return the reference of the vk::raii::pipeline
    auto get() const noexcept -> const vk::raii::Pipeline& {
        return handle_;
    }

    // return the bind point
    auto bind_point() const noexcept -> vk::PipelineBindPoint {
        return bind_point_;
    }

private:
    vk::raii::Pipeline handle_ = nullptr;
    vk::PipelineBindPoint bind_point_ = vk::PipelineBindPoint::eGraphics;
};

#endif
