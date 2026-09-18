#include "render/pass/forward/forward_pass.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "gfx/pipeline/graphics_pipeline.hpp"
#include "io/spirv_loader.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/registry/resource_registry.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    struct alignas(16) CameraUniforms {
        glm::mat4 view{1.0F};
        glm::mat4 projection{1.0F};
    };

    struct alignas(16) DrawConstants {
        glm::mat4 model{1.0F};
        glm::vec4 base_color_factor{1.0F};
        float alpha_cutoff = 0.5F;
        uint32_t alpha_mask = 0;
        float padding[2]{};
    };

    static_assert(sizeof(CameraUniforms) == 128);
    static_assert(sizeof(DrawConstants) == 96);

    auto create_camera_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{}
                .setBinding(0)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        };
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(
            create_info
        );
    }

    auto create_material_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{}
                .setBinding(0)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment),
            vk::DescriptorSetLayoutBinding{}
                .setBinding(1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment)
        };
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(
            create_info
        );
    }

    auto create_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& camera_layout,
        const vk::raii::DescriptorSetLayout& material_layout
    ) -> vk::raii::PipelineLayout {
        const std::array layouts{
            *camera_layout,
            *material_layout
        };
        const std::array push_constants{
            vk::PushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(DrawConstants)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(layouts)
            .setPushConstantRanges(push_constants);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path
    ) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        return device.logical_device().createShaderModule(
            vk::ShaderModuleCreateInfo{
                .codeSize = code.size() * sizeof(uint32_t),
                .pCode = code.data()
            }
        );
    }

    auto create_pipeline(
        const Device& device,
        const vk::raii::RenderPass& render_pass,
        const vk::raii::PipelineLayout& pipeline_layout
    ) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(
            device,
            "./spv/forward_vertex.spv"
        );
        const auto fragment_shader = create_shader_module(
            device,
            "./spv/forward_fragment.spv"
        );

        const vk::VertexInputBindingDescription vertex_binding{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex
        };
        const std::vector vertex_attributes{
            vk::VertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, position)
            },
            vk::VertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color)
            },
            vk::VertexInputAttributeDescription{
                .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, texcoord)
            }
        };

        GraphicsPipelineDesc desc{};
        desc.vertex_shader = &vertex_shader;
        desc.fragment_shader = &fragment_shader;
        desc.layout = &pipeline_layout;
        desc.render_pass = &render_pass;
        desc.vertex_bindings = {vertex_binding};
        desc.vertex_attributes = vertex_attributes;
        desc.depth_test_enable = true;
        desc.depth_write_enable = true;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto create_camera_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer,
                frame_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_material_descriptor_pool(
        const Device& device,
        uint32_t material_count
    ) -> vk::raii::DescriptorPool {
        const std::array pool_sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                material_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                material_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(material_count)
            .setPoolSizes(pool_sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_camera_buffers(
        const MemoryAllocator& allocator,
        uint32_t frame_count
    ) -> std::vector<Buffer> {
        std::vector<Buffer> buffers;
        buffers.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            buffers.emplace_back(
                allocator,
                BufferDesc{
                    .size = sizeof(CameraUniforms),
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                    .memory = BufferMemoryUsage::Upload,
                    .persistent_mapping = true
                }
            );
        }
        return buffers;
    }

    auto allocate_camera_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& descriptor_pool,
        const vk::raii::DescriptorSetLayout& layout,
        uint32_t frame_count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(
            frame_count,
            *layout
        );
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*descriptor_pool)
            .setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(
            allocate_info
        );
    }

    auto create_sampler(const Device& device) -> vk::raii::Sampler {
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setAnisotropyEnable(false)
            .setMaxAnisotropy(1.0F)
            .setMinLod(0.0F)
            .setMaxLod(std::numeric_limits<float>::max());
        return device.logical_device().createSampler(create_info);
    }

    auto create_command_pools(
        const Device& device,
        uint32_t frame_count
    ) -> std::vector<vk::raii::CommandPool> {
        std::vector<vk::raii::CommandPool> command_pools;
        command_pools.reserve(frame_count);
        for (uint32_t index = 0; index < frame_count; ++index) {
            command_pools.emplace_back(
                device.logical_device().createCommandPool(
                    vk::CommandPoolCreateInfo{
                        .flags = vk::CommandPoolCreateFlagBits::eTransient,
                        .queueFamilyIndex = device.graphics_family()
                    }
                )
            );
        }
        return command_pools;
    }

    auto allocate_command_buffers(
        const Device& device,
        const std::vector<vk::raii::CommandPool>& command_pools
    ) -> std::vector<vk::raii::CommandBuffer> {
        std::vector<vk::raii::CommandBuffer> command_buffers;
        command_buffers.reserve(command_pools.size());
        for (const auto& command_pool : command_pools) {
            vk::CommandBufferAllocateInfo allocate_info{};
            allocate_info
                .setCommandPool(*command_pool)
                .setLevel(vk::CommandBufferLevel::eSecondary)
                .setCommandBufferCount(1);
            auto allocated =
                device.logical_device().allocateCommandBuffers(
                    allocate_info
                );
            command_buffers.push_back(std::move(allocated.front()));
        }
        return command_buffers;
    }
}

ForwardPass::ForwardPass(
    const Device& device,
    const MemoryAllocator& allocator,
    const FramesInFlight& frames_in_flight,
    const Swapchain& swapchain,
    const ResourceRegistry& resources
) : device_(device),
    frames_in_flight_(frames_in_flight),
    swapchain_(swapchain),
    resources_(resources),
    camera_layout_(create_camera_layout(device)),
    material_layout_(create_material_layout(device)),
    pipeline_layout_(
        create_pipeline_layout(
            device,
            camera_layout_,
            material_layout_
        )
    ),
    pipeline_(
        create_pipeline(device, swapchain.render_pass(), pipeline_layout_)
    ),
    camera_descriptor_pool_(
        create_camera_descriptor_pool(device, frames_in_flight.count())
    ),
    material_descriptor_pool_(
        create_material_descriptor_pool(device, max_material_count_)
    ),
    camera_buffers_(
        create_camera_buffers(allocator, frames_in_flight.count())
    ),
    camera_descriptor_sets_(
        allocate_camera_descriptor_sets(
            device,
            camera_descriptor_pool_,
            camera_layout_,
            frames_in_flight.count()
        )
    ),
    sampler_(create_sampler(device)),
    command_pools_(
        create_command_pools(device, frames_in_flight.count())
    ),
    command_buffers_(allocate_command_buffers(device, command_pools_)) {}

auto ForwardPass::init() -> void {
    if (initialized_) {
        throw std::logic_error(
            "forward pass is already initialized!"
        );
    }

    for (uint32_t index = 0;
         index < frames_in_flight_.count();
         ++index) {
        const vk::DescriptorBufferInfo buffer_info{
            .buffer = camera_buffers_[index].get(),
            .offset = 0,
            .range = sizeof(CameraUniforms)
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*camera_descriptor_sets_[index])
                .setDstBinding(0)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setBufferInfo(buffer_info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
    }

    const auto material_count = resources_.material_count();
    if (material_count > max_material_count_) {
        throw std::length_error(
            "forward pass material capacity exceeded!"
        );
    }

    for (std::size_t index = 0; index < material_count; ++index) {
        const ResourceId<Material> material_id{
            static_cast<uint32_t>(index)
        };

        const auto& material = resources_.query(material_id);
        const auto& texture = resources_.query(
            material.base_color_texture_id()
        );

        const std::array layouts{*material_layout_};
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*material_descriptor_pool_)
            .setSetLayouts(layouts);
        auto descriptor_sets =
            device_.logical_device().allocateDescriptorSets(
                allocate_info
            );
        auto descriptor_set = std::move(descriptor_sets.front());

        const vk::DescriptorImageInfo sampler_info{
            .sampler = *sampler_
        };
        const vk::DescriptorImageInfo image_info{
            .imageView = *texture.image_view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*descriptor_set)
                .setDstBinding(0)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setImageInfo(sampler_info),
            vk::WriteDescriptorSet{}
                .setDstSet(*descriptor_set)
                .setDstBinding(1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(image_info)
        };
        device_.logical_device().updateDescriptorSets(writes, {});
        material_descriptor_sets_.try_emplace(
            material_id.value(),
            std::move(descriptor_set)
        );
    }
    initialized_ = true;
}

auto ForwardPass::prepare(
    const Scene& scene,
    uint32_t image_index
) -> void {
    if (!initialized_) {
        throw std::logic_error(
            "forward pass must be initialized before preparing!"
        );
    }
    static_cast<void>(swapchain_.framebuffers().at(image_index));

    const auto extent = swapchain_.extent();
    const auto aspect_ratio =
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const CameraUniforms uniforms{
        .view = scene.camera().view_matrix(),
        .projection = scene.camera().projection_matrix(aspect_ratio)
    };
    camera_buffers_.at(frames_in_flight_.current_index()).write(
        &uniforms,
        sizeof(uniforms)
    );

    scene_ = &scene;
    image_index_ = image_index;
}

auto ForwardPass::record() -> vk::CommandBuffer {
    if (!initialized_) {
        throw std::logic_error(
            "forward pass must be initialized before recording!"
        );
    }
    if (scene_ == nullptr) {
        throw std::logic_error(
            "forward pass must be prepared before recording!"
        );
    }

    const auto frame_index = frames_in_flight_.current_index();
    command_pools_.at(frame_index).reset();
    auto& command_buffer = command_buffers_.at(frame_index);

    vk::CommandBufferInheritanceInfo inheritance_info{};
    inheritance_info
        .setRenderPass(*swapchain_.render_pass())
        .setSubpass(0)
        .setFramebuffer(*swapchain_.framebuffers().at(image_index_));

    vk::CommandBufferBeginInfo begin_info{};
    begin_info
        .setFlags(
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
            vk::CommandBufferUsageFlagBits::eRenderPassContinue
        )
        .setPInheritanceInfo(&inheritance_info);
    command_buffer.begin(begin_info);

    command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_
    );

    const auto extent = swapchain_.extent();
    const vk::Viewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };
    const vk::Rect2D scissor{
        .offset = vk::Offset2D{0, 0},
        .extent = extent
    };
    command_buffer.setViewport(0, viewport);
    command_buffer.setScissor(0, scissor);

    const auto& vertex_buffer = resources_.vertex_buffer();
    const auto& index_buffer = resources_.index_buffer();
    const std::array vertex_buffers{vertex_buffer.get()};
    constexpr std::array<vk::DeviceSize, 1> offsets{0};
    command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
    command_buffer.bindIndexBuffer(
        index_buffer.get(),
        0,
        vk::IndexType::eUint32
    );

    const std::array camera_sets{
        *camera_descriptor_sets_.at(frame_index)
    };
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout_,
        0,
        camera_sets,
        {}
    );

    for (const auto& entity : scene_->entities()) {
        const auto* transform = entity.get_component<Transform>();
        const auto* mesh_renderer =
            entity.get_component<MeshRenderer>();
        if (transform == nullptr || mesh_renderer == nullptr) {
            continue;
        }

        const auto& model = resources_.query(
            mesh_renderer->model_id()
        );
        for (const auto& primitive : model.primitives()) {
            const auto& mesh = resources_.query(primitive.mesh);
            const auto& material = resources_.query(primitive.material);
            const auto& base_color = material.base_color_factor();
            const DrawConstants constants{
                .model = transform->model_matrix(),
                .base_color_factor = glm::vec4{
                    base_color[0],
                    base_color[1],
                    base_color[2],
                    base_color[3]
                },
                .alpha_cutoff = material.alpha_cutoff(),
                .alpha_mask = material.alpha_mask() ? 1U : 0U
            };
            command_buffer.pushConstants<DrawConstants>(
                *pipeline_layout_,
                vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment,
                0,
                constants
            );

            const std::array material_sets{
                *material_descriptor_sets_.at(
                    primitive.material.value()
                )
            };
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pipeline_layout_,
                1,
                material_sets,
                {}
            );
            command_buffer.drawIndexed(
                mesh.index_count,
                1,
                mesh.first_index,
                mesh.vertex_offset,
                0
            );
        }
    }

    command_buffer.end();
    return *command_buffer;
}

auto ForwardPass::recreate_pipeline(
    const vk::raii::RenderPass& render_pass
) -> void {
    pipeline_ = create_pipeline(device_, render_pass, pipeline_layout_);
}
