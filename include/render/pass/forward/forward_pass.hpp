#ifndef FORWARD_PASS_HPP
#define FORWARD_PASS_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "gfx/device/device.hpp"
#include "render/pass/forward/camera_writer.hpp"
#include "render/pass/forward/material_writer.hpp"

class MemoryAllocator;
class ResourceRegistry;
class Scene;
struct RenderFrameContext;

class ForwardPass {
public:
    ForwardPass(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::RenderPass& render_pass,
        uint32_t frame_count
    );

    ForwardPass(const ForwardPass&) = delete;
    auto operator=(const ForwardPass&) -> ForwardPass& = delete;
    ForwardPass(ForwardPass&&) = delete;
    auto operator=(ForwardPass&&) -> ForwardPass& = delete;

    auto write_material_descriptors(
        std::span<const ResourceId<Material>> material_ids,
        const ResourceRegistry& registry
    ) -> void;

    auto record(
        const RenderFrameContext& frame,
        const Scene& scene,
        const ResourceRegistry& registry
    ) -> void;

    auto recreate_pipeline(
        const Device& device,
        const vk::raii::RenderPass& render_pass
    ) -> void;

private:
    std::vector<vk::raii::DescriptorSetLayout> descriptor_set_layouts_;
    vk::raii::PipelineLayout pipeline_layout_ = nullptr;
    vk::raii::Pipeline pipeline_ = nullptr;
    CameraWriter camera_writer_;
    MaterialWriter material_writer_;
};

#endif
