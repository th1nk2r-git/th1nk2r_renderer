#ifndef RESTIR_DI_PASS_HPP
#define RESTIR_DI_PASS_HPP

#include <cstdint>
#include <memory>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "render/pass/render_pass.hpp"

class Buffer;
class MemoryAllocator;
class TlasBuildPass;

class RestirDiPass final : public RenderPass {
public:
    inline static constexpr std::string_view pass_name = "restir_di";
    inline static constexpr std::string_view current_resource = "restir_di_current";
    inline static constexpr std::string_view final_resource = "restir_di_final";
    inline static constexpr uint32_t reservoir_size = 32;

    struct Settings {
        uint32_t candidate_count = 1;
        uint32_t temporal_history_length = 5; // History M is capped at candidate_count * this value.
        uint32_t spatial_neighbor_count = 5;
        uint32_t spatial_radius = 30; // Pixels.
        float depth_threshold = 0.1F; // Relative linear depth difference.
        float normal_threshold = 0.9063078F; // Minimum normal cosine (25 degrees).
        float roughness_threshold = 0.2F;
        float metallic_threshold = 0.2F;
        float ray_bias = 0.01F;
        bool temporal_reuse = false;
        bool spatial_reuse = true;

        auto operator==(const Settings&) const -> bool = default;
    };

    RestirDiPass(const Device& device, const MemoryAllocator& allocator, RenderGraph& render_graph, const TlasBuildPass& tlas_build_pass);
    ~RestirDiPass() override;

    RestirDiPass(const RestirDiPass&) = delete;
    auto operator=(const RestirDiPass&) -> RestirDiPass& = delete;
    RestirDiPass(RestirDiPass&&) = delete;
    auto operator=(RestirDiPass&&) -> RestirDiPass& = delete;

    static auto declare_resources(const Device& device, RenderGraph& render_graph, vk::Extent2D extent) -> void;

    auto init() -> void override;
    auto configure(RenderGraph& render_graph) -> void override;
    auto prepare(const Scene& scene) -> void override;
    auto record() -> vk::CommandBuffer override;

    auto settings() const noexcept -> const Settings&;
    auto set_settings(const Settings& settings) -> void;
    auto reset_history() noexcept -> void;
    auto frame_buffer(uint32_t frame_index) const -> const Buffer&;
    auto light_buffer(uint32_t frame_index) const -> const Buffer&;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
