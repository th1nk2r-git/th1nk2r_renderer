#ifndef DENOISE_PASS_HPP
#define DENOISE_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "NRD.h"
#include "render/pass/render_pass.hpp"

class DeviceContext;

class DenoisePass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "denoise";
    inline static constexpr std::string_view diffuse_resource = "denoised_diffuse";
    inline static constexpr std::string_view specular_resource = "denoised_specular";

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

    DenoisePass(const DeviceContext& device_context, RenderGraph& render_graph, vk::Extent2D extent, Settings settings = {});
    ~DenoisePass() override;

    DenoisePass(const DenoisePass&) = delete;
    auto operator=(const DenoisePass&) -> DenoisePass& = delete;
    DenoisePass(DenoisePass&&) = delete;
    auto operator=(DenoisePass&&) -> DenoisePass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
