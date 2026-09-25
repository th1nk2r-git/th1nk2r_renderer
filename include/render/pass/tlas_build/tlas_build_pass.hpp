#ifndef TLAS_BUILD_PASS_HPP
#define TLAS_BUILD_PASS_HPP

#include <cstdint>
#include <memory>
#include <string_view>

#include <vulkan/vulkan_raii.hpp>

#include "render/pass/render_pass.hpp"

class AssetsDB;
class MemoryAllocator;

class TlasBuildPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "tlas_build";
    inline static constexpr uint32_t max_instance_count = 65'536;

    TlasBuildPass(
        const Device& device,
        const MemoryAllocator& allocator,
        RenderGraph& render_graph,
        const AssetsDB& assets
    );
    ~TlasBuildPass() override;

    TlasBuildPass(const TlasBuildPass&) = delete;
    auto operator=(const TlasBuildPass&) -> TlasBuildPass& = delete;
    TlasBuildPass(TlasBuildPass&&) = delete;
    auto operator=(TlasBuildPass&&) -> TlasBuildPass& = delete;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

    auto handle(uint32_t frame_index) const
        -> const vk::raii::AccelerationStructureKHR&;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
