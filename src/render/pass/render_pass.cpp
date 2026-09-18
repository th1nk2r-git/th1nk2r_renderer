#include "render/pass/render_pass.hpp"

#include <stdexcept>
#include <utility>

RenderPass::RenderPass(
    std::string name,
    const Device& device,
    FramesInFlight& frames_in_flight,
    ImageRegistry& images,
    BufferRegistry& buffers
) : device_(device),
    frames_in_flight_(frames_in_flight),
    images_(images),
    buffers_(buffers),
    name_(std::move(name)) {
    if (name_.empty()) {
        throw std::invalid_argument("render pass name cannot be empty!");
    }
}

auto RenderPass::prepare(const Scene&) -> void {
}
