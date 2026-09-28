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
    // RGB: sRGB base color; A: linear material AO.
    inline static constexpr std::string_view base_color_ao_resource = "base_color_ao";
    // RGB: normalized world-space normal XYZ in [-1, 1]; A: linear roughness.
    inline static constexpr std::string_view normal_roughness_resource = "normal_roughness";
    // RGB: linear HDR emissive; A: metallic.
    inline static constexpr std::string_view emissive_metallic_resource = "emissive_metallic";
    // XYZ: previous UV/view-depth minus current UV/view-depth; W: valid motion.
    inline static constexpr std::string_view motion_resource = "motion";
    inline static constexpr std::string_view depth_resource = "depth";

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
