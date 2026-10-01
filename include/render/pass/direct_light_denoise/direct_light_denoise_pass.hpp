#ifndef DIRECT_LIGHT_DENOISE_PASS_HPP
#define DIRECT_LIGHT_DENOISE_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "NRD.h"
#include "render/pass/render_pass.hpp"

class DeviceContext;

class DirectLightDenoisePass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "direct_light_denoise";
    inline static constexpr std::string_view diffuse_resource = "direct_light_diffuse_denoised";
    inline static constexpr std::string_view specular_resource = "direct_light_specular_denoised";

    struct Settings {
        nrd::RelaxSettings relax{};
        float denoising_range = 500000.0F;

        Settings() {
            relax.atrousIterationNum = 1;
            relax.diffusePrepassBlurRadius = 30.0F;
            relax.specularPrepassBlurRadius = 50.0F;
            relax.diffuseMaxAccumulatedFrameNum = 30;
            relax.specularMaxAccumulatedFrameNum = 30;
            relax.minMaterialForDiffuse = 0.0F;
        }
    };

    DirectLightDenoisePass(const DeviceContext& device_context, RenderGraph& render_graph, vk::Extent2D extent, Settings settings = {});
    ~DirectLightDenoisePass() override;

    DirectLightDenoisePass(const DirectLightDenoisePass&) = delete;
    auto operator=(const DirectLightDenoisePass&) -> DirectLightDenoisePass& = delete;
    DirectLightDenoisePass(DirectLightDenoisePass&&) = delete;
    auto operator=(DirectLightDenoisePass&&) -> DirectLightDenoisePass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
