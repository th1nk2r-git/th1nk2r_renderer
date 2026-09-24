#ifndef CULLING_PASS_HPP
#define CULLING_PASS_HPP

#include <cstdint>
#include <memory>
#include <string_view>

#include "render/pass/render_pass.hpp"

class AssetsDB;

class CullingPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "culling";
    inline static constexpr std::string_view instance_resource =
        "render_instances";
    inline static constexpr std::string_view command_resource =
        "indirect_draw_commands";
    inline static constexpr std::string_view count_resource =
        "indirect_draw_count";
    inline static constexpr uint32_t max_instance_count = 65'536;

    CullingPass(
        const Device& device,
        RenderGraph& render_graph,
        const AssetsDB& assets
    );
    ~CullingPass() override;

    CullingPass(const CullingPass&) = delete;
    auto operator=(const CullingPass&) -> CullingPass& = delete;
    CullingPass(CullingPass&&) = delete;
    auto operator=(CullingPass&&) -> CullingPass& = delete;

    static auto declare_resources(RenderGraph& render_graph) -> void;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
