#ifndef GEOMETRY_PASS_HPP
#define GEOMETRY_PASS_HPP

#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class AssetsDB;
class MemoryAllocator;

class GeometryPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "geometry";
    inline static constexpr std::string_view base_color_ao_resource = "gbuffer_base_color_ao";
    inline static constexpr std::string_view normal_rm_resource = "gbuffer_normal_rm";
    inline static constexpr std::string_view emissive_resource = "gbuffer_emissive";
    inline static constexpr std::string_view depth_resource = "gbuffer_depth";

    GeometryPass(
        const Device& device,
        const MemoryAllocator& allocator,
        RenderGraph& render_graph,
        const AssetsDB& assets
    );
    ~GeometryPass() override;

    GeometryPass(const GeometryPass&) = delete;
    auto operator=(const GeometryPass&) -> GeometryPass& = delete;
    GeometryPass(GeometryPass&&) = delete;
    auto operator=(GeometryPass&&) -> GeometryPass& = delete;

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
