#ifndef LIGHT_COMPOSITE_PASS_HPP
#define LIGHT_COMPOSITE_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class MemoryAllocator;

class LightCompositePass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "light_composite";
    inline static constexpr std::string_view output_resource = "backbuffer";

    LightCompositePass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph);
    ~LightCompositePass() override;

    LightCompositePass(const LightCompositePass&) = delete;
    auto operator=(const LightCompositePass&) -> LightCompositePass& = delete;
    LightCompositePass(LightCompositePass&&) = delete;
    auto operator=(LightCompositePass&&) -> LightCompositePass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
