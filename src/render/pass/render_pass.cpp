#include "render/pass/render_pass.hpp"

#include <stdexcept>
#include <utility>

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
