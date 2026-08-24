#ifndef FORWARD_PASS_HPP
#define FORWARD_PASS_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "gfx/device/device.hpp"
#include "render/pass/forward/camera_writer.hpp"
#include "render/pass/forward/ibl_writer.hpp"
#include "render/pass/forward/light_writer.hpp"
#include "render/pass/forward/material_writer.hpp"
#include "render/pass/shadow/shadow_pass.hpp"

class MemoryAllocator;
class ResourceRegistry;
class Scene;

class ForwardPass {
public:
    struct ExecutionContext {
        vk::raii::CommandBuffer& command_buffer;
        uint32_t frame_index = 0;
    };

    struct Input {
        const Scene& scene;
        const ResourceRegistry& registry;
        const ShadowPass::Output& shadow;
    };

    struct Output {
        const vk::raii::RenderPass& render_pass;
        const vk::raii::Framebuffer& framebuffer;
        vk::Extent2D extent;
    };

    ForwardPass(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::RenderPass& render_pass,
        const vk::raii::DescriptorSetLayout& shadow_descriptor_set_layout,
        uint32_t frame_count
    );

    ForwardPass(const ForwardPass&) = delete;
    auto operator=(const ForwardPass&) -> ForwardPass& = delete;
    ForwardPass(ForwardPass&&) = delete;
    auto operator=(ForwardPass&&) -> ForwardPass& = delete;

    auto write_material(
        std::span<const ResourceId<Material>> material_ids,
        const ResourceRegistry& registry
    ) -> void;

    auto write_environment(
        const HdrImageData& panorama,
        ImageUploader& uploader
    ) -> void;

    auto record(ExecutionContext context, Input input, Output output) -> void;

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
    LightWriter light_writer_;
    IblWriter ibl_writer_;
};

#endif
