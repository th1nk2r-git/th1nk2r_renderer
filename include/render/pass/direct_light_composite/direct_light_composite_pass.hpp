#ifndef DIRECT_LIGHT_COMPOSITE_PASS_HPP
#define DIRECT_LIGHT_COMPOSITE_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class RestirDiPass;

class DirectLightCompositePass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "direct_light_composite";
    inline static constexpr std::string_view output_resource = "backbuffer";

    DirectLightCompositePass(const Device& device, RenderGraph& render_graph, const RestirDiPass& restir_di_pass);
    ~DirectLightCompositePass() override;

    DirectLightCompositePass(const DirectLightCompositePass&) = delete;
    auto operator=(const DirectLightCompositePass&) -> DirectLightCompositePass& = delete;
    DirectLightCompositePass(DirectLightCompositePass&&) = delete;
    auto operator=(DirectLightCompositePass&&) -> DirectLightCompositePass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
