#include "render/pass/culling/culling_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "gfx/device/device.hpp"
#include "gfx/resource/buffer.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/geometry/geometry_pass.hpp"
#include "render/render_graph.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t thread_count = 64;

    struct alignas(16) GpuRenderInstance {
        glm::mat4 model{1.0F};
        glm::vec4 normal_column_0{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_1{0.0F, 1.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_2{0.0F, 0.0F, 1.0F, 0.0F};
        glm::vec4 bounds_minimum{0.0F};
        glm::vec4 bounds_maximum{0.0F};
        uint32_t index_count = 0;
        uint32_t first_index = 0;
        int32_t vertex_offset = 0;
        uint32_t material_index = 0;
    };

    struct alignas(16) GpuCullingConstants {
        std::array<glm::vec4, 6> frustum_planes{};
        uint32_t instance_count = 0;
        uint32_t max_draw_count = 0;
        uint32_t padding_0 = 0;
        uint32_t padding_1 = 0;
    };

    static_assert(sizeof(GpuRenderInstance) == 160);
    static_assert(offsetof(GpuRenderInstance, model) == 0);
    static_assert(offsetof(GpuRenderInstance, normal_column_0) == 64);
    static_assert(offsetof(GpuRenderInstance, bounds_minimum) == 112);
    static_assert(offsetof(GpuRenderInstance, index_count) == 144);
    static_assert(offsetof(GpuRenderInstance, material_index) == 156);
    static_assert(sizeof(GpuCullingConstants) == 112);
    static_assert(sizeof(vk::DrawIndexedIndirectCommand) == 20);

    struct CommandSlot {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
    };

    auto matrix_row(const glm::mat4& matrix, std::size_t row) noexcept
        -> glm::vec4 {
        return {
            matrix[0][row],
            matrix[1][row],
            matrix[2][row],
            matrix[3][row]
        };
    }

    auto frustum_planes(const glm::mat4& view_projection) noexcept
        -> std::array<glm::vec4, 6> {
        const auto row_0 = matrix_row(view_projection, 0);
        const auto row_1 = matrix_row(view_projection, 1);
        const auto row_2 = matrix_row(view_projection, 2);
        const auto row_3 = matrix_row(view_projection, 3);

        // Vulkan clip space uses -w <= x,y <= w and 0 <= z <= w.
        return {
            row_3 + row_0,
            row_3 - row_0,
            row_3 + row_1,
            row_3 - row_1,
            row_2,
            row_3 - row_2
        };
    }

    auto create_descriptor_set_layout(const Device& device)
        -> vk::raii::DescriptorSetLayout {
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
            bindings[binding]
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute);
        }

        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout
    ) -> vk::raii::PipelineLayout {
        if (sizeof(GpuCullingConstants) >
            device.physical_device().getProperties()
                .limits.maxPushConstantsSize) {
            throw std::runtime_error(
                "culling constants exceed maxPushConstantsSize"
            );
        }

        const std::array layouts{*descriptor_set_layout};
        const std::array ranges{
            vk::PushConstantRange{
                vk::ShaderStageFlagBits::eCompute,
                0,
                sizeof(GpuCullingConstants)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(layouts)
            .setPushConstantRanges(ranges);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path
    ) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        const vk::ShaderModuleCreateInfo create_info{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()
        };
        return device.logical_device().createShaderModule(create_info);
    }

    auto create_pipeline(
        const Device& device,
        const vk::raii::PipelineLayout& pipeline_layout
    ) -> vk::raii::Pipeline {
        const auto shader = create_shader_module(
            device,
            "./spv/culling.spv"
        );
        vk::PipelineShaderStageCreateInfo stage{};
        stage
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(*shader)
            .setPName("main");
        vk::ComputePipelineCreateInfo create_info{};
        create_info
            .setStage(stage)
            .setLayout(*pipeline_layout);
        return device.logical_device().createComputePipeline(
            nullptr,
            create_info
        );
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        if (frame_count > std::numeric_limits<uint32_t>::max() / 3) {
            throw std::overflow_error(
                "culling descriptor count exceeds uint32_t"
            );
        }
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eStorageBuffer,
                frame_count * 3
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto allocate_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& pool,
        const vk::raii::DescriptorSetLayout& layout,
        uint32_t frame_count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(
            frame_count,
            *layout
        );
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*pool)
            .setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(allocate_info);
    }

    auto create_command_slots(
        const Device& device,
        uint32_t frame_count
    ) -> std::vector<CommandSlot> {
        std::vector<CommandSlot> slots;
        slots.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            auto pool = device.logical_device().createCommandPool(
                vk::CommandPoolCreateInfo{
                    .flags = vk::CommandPoolCreateFlagBits::eTransient,
                    .queueFamilyIndex = device.graphics_family()
                }
            );
            const vk::CommandBufferAllocateInfo allocate_info{
                .commandPool = *pool,
                .level = vk::CommandBufferLevel::eSecondary,
                .commandBufferCount = 1
            };
            auto command_buffers =
                device.logical_device().allocateCommandBuffers(allocate_info);
            slots.push_back(CommandSlot{
                .pool = std::move(pool),
                .command_buffer = std::move(command_buffers.front())
            });
        }
        return slots;
    }

    auto require_indirect_capacity(const Device& device) -> void {
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(*device.physical_device(), &properties);

        if (CullingPass::max_instance_count >
            properties.properties.limits.maxDrawIndirectCount) {
            throw std::runtime_error(
                "culling capacity exceeds maxDrawIndirectCount"
            );
        }
        const auto groups =
            (CullingPass::max_instance_count + thread_count - 1) /
            thread_count;
        if (groups > properties.properties.limits.maxComputeWorkGroupCount[0]) {
            throw std::runtime_error(
                "culling dispatch exceeds maxComputeWorkGroupCount[0]"
            );
        }
    }
}

struct CullingPass::Impl {
    explicit Impl(const AssetsDB& asset_database) : assets(asset_database) {}

    const AssetsDB& assets;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;
    std::vector<CommandSlot> command_slots;
    std::vector<GpuCullingConstants> constants;
    std::vector<GpuRenderInstance> cpu_instances;
    bool initialized = false;
};

CullingPass::CullingPass(
    const Device& device,
    RenderGraph& render_graph,
    const AssetsDB& assets
) : RenderPass(std::string{pass_name}, device, render_graph),
    impl_(std::make_unique<Impl>(assets)) {}

CullingPass::~CullingPass() = default;

auto CullingPass::declare_resources(RenderGraph& render_graph) -> void {
    render_graph.create_buffer(
        std::string{instance_resource},
        BufferDesc{
            .size = sizeof(GpuRenderInstance) * max_instance_count,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        },
        ResourceMultiplicity::PerFrame
    );
    render_graph.create_buffer(
        std::string{command_resource},
        BufferDesc{
            .size = sizeof(vk::DrawIndexedIndirectCommand) *
                max_instance_count,
            .memory = BufferMemoryUsage::GpuOnly
        },
        ResourceMultiplicity::PerFrame
    );
    render_graph.create_buffer(
        std::string{count_resource},
        BufferDesc{
            .size = sizeof(uint32_t),
            .usage = vk::BufferUsageFlagBits::eTransferDst,
            .memory = BufferMemoryUsage::GpuOnly
        },
        ResourceMultiplicity::PerFrame
    );
}

auto CullingPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("culling pass is already initialized");
    }
    require_indirect_capacity(device_);

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->descriptor_set_layout = create_descriptor_set_layout(device_);
    impl_->pipeline_layout = create_pipeline_layout(
        device_,
        impl_->descriptor_set_layout
    );
    impl_->pipeline = create_pipeline(device_, impl_->pipeline_layout);
    impl_->descriptor_pool = create_descriptor_pool(device_, frame_count);
    impl_->descriptor_sets = allocate_descriptor_sets(
        device_,
        impl_->descriptor_pool,
        impl_->descriptor_set_layout,
        frame_count
    );

    for (uint32_t index = 0; index < frame_count; ++index) {
        const auto& instances = render_graph_.buffer(
            instance_resource,
            index
        );
        const auto& commands = render_graph_.buffer(
            command_resource,
            index
        );
        const auto& count = render_graph_.buffer(count_resource, index);
        const std::array buffer_infos{
            vk::DescriptorBufferInfo{
                .buffer = instances.get(),
                .offset = 0,
                .range = instances.size()
            },
            vk::DescriptorBufferInfo{
                .buffer = commands.get(),
                .offset = 0,
                .range = commands.size()
            },
            vk::DescriptorBufferInfo{
                .buffer = count.get(),
                .offset = 0,
                .range = count.size()
            }
        };
        std::array<vk::WriteDescriptorSet, 3> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            writes[binding]
                .setDstSet(*impl_->descriptor_sets[index])
                .setDstBinding(binding)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(buffer_infos[binding]);
        }
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    impl_->command_slots = create_command_slots(device_, frame_count);
    impl_->constants.resize(frame_count);
    impl_->cpu_instances.reserve(max_instance_count);
    impl_->initialized = true;
}

auto CullingPass::configure(RenderGraph& render_graph) -> void {
    render_graph.set_buffer_usage(
        name(),
        instance_resource,
        BufferUsage::ComputeStorageRead
    );
    render_graph.set_buffer_usage(
        name(),
        command_resource,
        BufferUsage::ComputeStorageWrite
    );
    render_graph.set_buffer_usage(
        name(),
        count_resource,
        BufferUsage::ComputeStorageWrite
    );
}

auto CullingPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error(
            "culling pass must be initialized before preparation"
        );
    }

    impl_->cpu_instances.clear();
    for (const auto& entity : scene.entities()) {
        const auto* mesh_renderer = entity.get_component<MeshRenderer>();
        if (mesh_renderer == nullptr) {
            continue;
        }
        const auto* transform = entity.get_component<Transform>();
        const auto model = transform == nullptr
            ? glm::mat4{1.0F}
            : transform->model_matrix();
        const auto normal_matrix = glm::transpose(
            glm::inverse(glm::mat3{model})
        );
        const auto& render_model = impl_->assets.query(
            mesh_renderer->model_id()
        );

        for (const auto& primitive : render_model.primitives()) {
            if (impl_->cpu_instances.size() >= max_instance_count) {
                throw std::length_error(
                    "render instance count exceeds culling capacity"
                );
            }
            const auto& mesh = impl_->assets.query(primitive.mesh);
            const auto& material = impl_->assets.query(primitive.material);
            impl_->cpu_instances.push_back(GpuRenderInstance{
                .model = model,
                .normal_column_0 = glm::vec4{normal_matrix[0], 0.0F},
                .normal_column_1 = glm::vec4{normal_matrix[1], 0.0F},
                .normal_column_2 = glm::vec4{normal_matrix[2], 0.0F},
                .bounds_minimum = glm::vec4{mesh.bounds.minimum, 0.0F},
                .bounds_maximum = glm::vec4{mesh.bounds.maximum, 0.0F},
                .index_count = mesh.index_count,
                .first_index = mesh.first_index,
                .vertex_offset = mesh.vertex_offset,
                .material_index = material.buffer_index
            });
        }
    }

    const auto frame_index = render_graph_.current_frame_index();
    if (!impl_->cpu_instances.empty()) {
        render_graph_.buffer(instance_resource).write(
            impl_->cpu_instances.data(),
            sizeof(GpuRenderInstance) * impl_->cpu_instances.size()
        );
    }

    const auto extent = render_graph_.image(
        GeometryPass::base_color_ao_resource
    ).extent();
    const auto aspect_ratio = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const auto view_projection =
        scene.camera().projection_matrix(aspect_ratio) *
        scene.camera().view_matrix();
    impl_->constants[frame_index] = GpuCullingConstants{
        .frustum_planes = frustum_planes(view_projection),
        .instance_count =
            static_cast<uint32_t>(impl_->cpu_instances.size()),
        .max_draw_count = max_instance_count
    };
}

auto CullingPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error(
            "culling pass must be initialized before recording"
        );
    }

    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->command_slots.at(frame_index);
    slot.pool.reset();

    vk::CommandBufferInheritanceInfo inheritance{};
    vk::CommandBufferBeginInfo begin_info{};
    begin_info
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
        .setPInheritanceInfo(&inheritance);

    auto& command_buffer = slot.command_buffer;
    command_buffer.begin(begin_info);

    const auto& count_buffer = render_graph_.buffer(count_resource);
    command_buffer.fillBuffer(
        count_buffer.get(),
        0,
        sizeof(uint32_t),
        0
    );
    const std::array count_barriers{
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(
                vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite
            )
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(count_buffer.get())
            .setOffset(0)
            .setSize(sizeof(uint32_t))
    };
    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        count_barriers,
        {}
    );

    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eCompute,
        *impl_->pipeline
    );
    const std::array descriptor_sets{
        *impl_->descriptor_sets.at(frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *impl_->pipeline_layout,
        0,
        descriptor_sets,
        {}
    );
    command_buffer.pushConstants<GpuCullingConstants>(
        *impl_->pipeline_layout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        impl_->constants.at(frame_index)
    );

    const auto instance_count = impl_->constants[frame_index].instance_count;
    if (instance_count > 0) {
        command_buffer.dispatch(
            (instance_count + thread_count - 1) / thread_count,
            1,
            1
        );
    }

    command_buffer.end();
    return *command_buffer;
}
