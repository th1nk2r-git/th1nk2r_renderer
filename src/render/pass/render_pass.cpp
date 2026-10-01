#include "render/pass/render_pass.hpp"

#include <stdexcept>
#include <utility>

#include "gfx/device/device.hpp"

RenderPass::RenderPass(
    std::string name,
    const Device& device,
    RenderGraph& render_graph
) : device_(device),
    render_graph_(render_graph),
    name_(std::move(name)) {
    if (name_.empty()) {
        throw std::invalid_argument("render pass name cannot be empty!");
    }
}

auto RenderPass::prepare(const Scene&) -> void {
}

#ifndef NDEBUG
auto RenderPass::insert_debug_marker(
    const vk::raii::CommandBuffer& command_buffer,
    const char* name
) const -> void {
    if (!device_.debug_utils_enabled() ||
        command_buffer.getDispatcher()->vkCmdInsertDebugUtilsLabelEXT == nullptr) {
        return;
    }

    vk::DebugUtilsLabelEXT label{};
    label.pLabelName = name;
    command_buffer.insertDebugUtilsLabelEXT(label);
}
#endif
