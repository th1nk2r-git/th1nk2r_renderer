#ifndef RENDER_PASS_HPP
#define RENDER_PASS_HPP

#include <string>
#include <string_view>

#include <vulkan/vulkan.hpp>

class Device;
class RenderGraph;
class Scene;

class RenderPass {
public:
    virtual ~RenderPass() = default;

    RenderPass(const RenderPass&) = delete;
    auto operator=(const RenderPass&) -> RenderPass& = delete;
    RenderPass(RenderPass&&) = delete;
    auto operator=(RenderPass&&) -> RenderPass& = delete;

    auto name() const noexcept -> std::string_view {
        return name_;
    }

    virtual auto init() -> void = 0;
    virtual auto configure(RenderGraph& render_graph) -> void = 0;
    virtual auto prepare(const Scene& scene) -> void;
    virtual auto record() -> vk::CommandBuffer = 0;

protected:
    RenderPass(
        std::string name,
        const Device& device,
        RenderGraph& render_graph
    );

    const Device& device_;
    RenderGraph& render_graph_;

private:
    std::string name_;
};

#endif
