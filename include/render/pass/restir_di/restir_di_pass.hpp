#ifndef RESTIR_DI_PASS_HPP
#define RESTIR_DI_PASS_HPP

#include <memory>
#include <string_view>

#include "render/pass/render_pass.hpp"

class MemoryAllocator;

class RestirDiPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "restir_di";
    inline static constexpr std::string_view current_resource = "restir_di_current";
    inline static constexpr std::string_view final_resource = "restir_di_final";

    // Sampling controls. A reservoir always stores one selected sample.
    // Number of new light/sample-point proposals per pixel each frame.
    inline static constexpr uint32_t candidates_per_pixel = 8;
    // Number of nearby reservoirs considered in the second dispatch.
    inline static constexpr uint32_t spatial_neighbors_per_pixel = 4;
    // Maximum offset of those neighbors, in pixels.
    inline static constexpr uint32_t spatial_radius_pixels = 2;
    // Limit the effective M contributed by each reused reservoir; this is
    // not a storage capacity or a limit on the final accumulated M.
    inline static constexpr uint32_t max_reused_sample_count = 32;

    RestirDiPass(
        const Device& device,
        const MemoryAllocator& allocator,
        RenderGraph& render_graph
    );
    ~RestirDiPass() override;

    RestirDiPass(const RestirDiPass&) = delete;
    auto operator=(const RestirDiPass&) -> RestirDiPass& = delete;
    RestirDiPass(RestirDiPass&&) = delete;
    auto operator=(RestirDiPass&&) -> RestirDiPass& = delete;

    static auto declare_resources(
        const Device& device,
        RenderGraph& render_graph,
        vk::Extent2D extent
    ) -> void;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
