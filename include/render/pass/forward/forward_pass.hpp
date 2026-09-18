#ifndef FORWARD_PASS_HPP
#define FORWARD_PASS_HPP

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/frame/frames_in_flight.hpp"
#include "gfx/frame/swapchain.hpp"
#include "gfx/resource/buffer.hpp"

class MemoryAllocator;
class ResourceRegistry;
class Scene;

class ForwardPass {
public:
    ForwardPass(
        const Device& device,
        const MemoryAllocator& allocator,
        const FramesInFlight& frames_in_flight,
        const Swapchain& swapchain,
        const ResourceRegistry& resources
    );

    ForwardPass(const ForwardPass&) = delete;
    auto operator=(const ForwardPass&) -> ForwardPass& = delete;
    ForwardPass(ForwardPass&&) = delete;
    auto operator=(ForwardPass&&) -> ForwardPass& = delete;

    auto init() -> void;

    auto prepare(const Scene& scene, uint32_t image_index) -> void;
    auto record() -> vk::CommandBuffer;

    auto recreate_pipeline(
        const vk::raii::RenderPass& render_pass
    ) -> void;

private:
    static constexpr uint32_t max_material_count_ = 1024;

    const Device& device_;
    const FramesInFlight& frames_in_flight_;
    const Swapchain& swapchain_;
    const ResourceRegistry& resources_;

    const Scene* scene_ = nullptr;
    uint32_t image_index_ = 0;
    bool initialized_ = false;

    vk::raii::DescriptorSetLayout camera_layout_ = nullptr;
    vk::raii::DescriptorSetLayout material_layout_ = nullptr;
    vk::raii::PipelineLayout pipeline_layout_ = nullptr;
    vk::raii::Pipeline pipeline_ = nullptr;

    vk::raii::DescriptorPool camera_descriptor_pool_ = nullptr;
    vk::raii::DescriptorPool material_descriptor_pool_ = nullptr;

    std::vector<Buffer> camera_buffers_;
    std::vector<vk::raii::DescriptorSet> camera_descriptor_sets_;

    vk::raii::Sampler sampler_ = nullptr;
    std::unordered_map<uint32_t, vk::raii::DescriptorSet> material_descriptor_sets_;
    
    std::vector<vk::raii::CommandPool> command_pools_;
    std::vector<vk::raii::CommandBuffer> command_buffers_;
};

#endif
