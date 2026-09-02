#ifndef SHADOW_PASS_HPP
#define SHADOW_PASS_HPP

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "resource/gpu/resource_id.hpp"

class Material;
class ResourceRegistry;
class Scene;
class ShadowMaterialWriter;

struct PointLightShadowBinding {
    int32_t shadow_index = -1;
    float near_plane = 0.1F;
    float far_plane = 25.0F;
    float source_radius = 0.15F;
};

class ShadowPass {
public:
    struct Config {
        uint32_t resolution = 512;
        uint32_t max_shadow_light_count = 8;
        float minimum_bias = 0.004F;
        float maximum_filter_angle = 0.12F;
    };

    struct ExecutionContext {
        vk::raii::CommandBuffer& command_buffer;
        uint32_t frame_index = 0;
    };

    struct Input {
        const Scene& scene;
        const ResourceRegistry& registry;
    };

    struct Output {
        vk::DescriptorSet descriptor_set{};
        std::span<const PointLightShadowBinding> light_bindings;
        uint32_t shadow_light_count = 0;
    };

    ShadowPass(
        const Device& device,
        const MemoryAllocator& allocator,
        uint32_t frame_count,
        Config config = {}
    );
    ~ShadowPass();

    ShadowPass(const ShadowPass&) = delete;
    auto operator=(const ShadowPass&) -> ShadowPass& = delete;
    ShadowPass(ShadowPass&&) = delete;
    auto operator=(ShadowPass&&) -> ShadowPass& = delete;

    auto write_material(
        std::span<const ResourceId<Material>> material_ids,
        const ResourceRegistry& registry
    ) -> void;

    auto descriptor_set_layout() const noexcept
        -> const vk::raii::DescriptorSetLayout& {
        return output_descriptor_set_layout_;
    }

    auto prepare(uint32_t frame_index, const Scene& scene) -> Output;

    auto record(
        ExecutionContext context,
        Input input,
        const Output& output
    ) -> void;

private:
    struct FrameResources;

    Config config_;
    vk::Format depth_format_ = vk::Format::eUndefined;
    vk::raii::DescriptorSetLayout face_descriptor_set_layout_ = nullptr;
    vk::raii::DescriptorSetLayout output_descriptor_set_layout_ = nullptr;
    std::unique_ptr<ShadowMaterialWriter> material_writer_;
    vk::raii::PipelineLayout pipeline_layout_ = nullptr;
    vk::raii::RenderPass render_pass_ = nullptr;
    vk::raii::Pipeline pipeline_ = nullptr;
    vk::DeviceSize face_frame_stride_ = 0;
    Buffer face_buffer_;
    Buffer sampling_parameter_buffer_;
    vk::raii::DescriptorPool face_descriptor_pool_ = nullptr;
    std::vector<vk::raii::DescriptorSet> face_descriptor_sets_;
    vk::raii::DescriptorPool output_descriptor_pool_ = nullptr;
    std::vector<vk::raii::DescriptorSet> output_descriptor_sets_;
    vk::raii::Sampler shadow_sampler_ = nullptr;
    std::vector<std::unique_ptr<FrameResources>> frame_resources_;
    std::vector<std::vector<PointLightShadowBinding>> light_bindings_;
};

#endif
