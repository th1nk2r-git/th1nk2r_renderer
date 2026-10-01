#ifndef DIRECT_LIGHT_PASS_HPP
#define DIRECT_LIGHT_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class RestirDiPass;
class TlasBuildPass;

class DirectLightPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "direct_light";
    inline static constexpr std::string_view diffuse_resource = "direct_light_diffuse";
    inline static constexpr std::string_view specular_resource = "direct_light_specular";

    static auto declare_resources(const Device& device, RenderGraph& render_graph, vk::Extent2D extent) -> void;

    DirectLightPass(
        const Device& device,
        RenderGraph& render_graph,
        const TlasBuildPass& tlas_build_pass,
        const RestirDiPass& restir_di_pass
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
