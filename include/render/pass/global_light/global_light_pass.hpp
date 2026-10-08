#ifndef GLOBAL_LIGHT_PASS_HPP
#define GLOBAL_LIGHT_PASS_HPP

#include <cstdint>
#include <memory>
#include <string_view>

#include <vulkan/vulkan_raii.hpp>

#include "render/pass/render_pass.hpp"

class AssetsDB;
class MemoryAllocator;
class TlasBuildPass;

class GlobalLightPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "global_light";
    inline static constexpr std::string_view diffuse_resource = "global_light_diffuse";
    inline static constexpr std::string_view specular_resource = "global_light_specular";

    struct Settings {
        uint32_t samples_per_pixel = 1;
        uint32_t max_bounces = 6;
        uint32_t russian_roulette_start_bounce = 2;
        float russian_roulette_min_survival = 0.05F;
        float russian_roulette_max_survival = 0.95F;
        float ray_bias = 0.001F;
        float max_ray_distance = 10000.0F;
        bool cull_backfaces = true;
    };

    GlobalLightPass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph,
                    const AssetsDB& assets, const TlasBuildPass& tlas_build_pass, vk::Extent2D extent,
                    Settings settings = {});
    ~GlobalLightPass() override;

    GlobalLightPass(const GlobalLightPass&) = delete;
    auto operator=(const GlobalLightPass&) -> GlobalLightPass& = delete;
    GlobalLightPass(GlobalLightPass&&) = delete;
    auto operator=(GlobalLightPass&&) -> GlobalLightPass& = delete;

    static auto declare_resources(RenderGraph& render_graph, vk::Extent2D extent) -> void;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
