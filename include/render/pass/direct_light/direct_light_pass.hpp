#ifndef DIRECT_LIGHT_PASS_HPP
#define DIRECT_LIGHT_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class MemoryAllocator;

class DirectLightPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "direct_light";
    inline static constexpr std::string_view output_resource = "backbuffer";

    DirectLightPass(
        const Device& device,
        const MemoryAllocator& allocator,
        RenderGraph& render_graph
    );
    ~DirectLightPass() override;

    DirectLightPass(const DirectLightPass&) = delete;
    auto operator=(const DirectLightPass&) -> DirectLightPass& = delete;
    DirectLightPass(DirectLightPass&&) = delete;
    auto operator=(DirectLightPass&&) -> DirectLightPass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
