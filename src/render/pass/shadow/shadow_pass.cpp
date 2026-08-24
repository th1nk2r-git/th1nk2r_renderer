#include "render/pass/shadow/shadow_pass.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resource/image.hpp"
#include "io/spirv_loader.hpp"
#include "render/pass/shadow/shadow_material_writer.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/registry/resource_registry.hpp"
#include "scene/scene.hpp"

namespace {
    constexpr uint32_t cube_face_count = 6;

    struct alignas(16) GpuShadowFace {
        glm::mat4 light_view_projection{1.0F};
        glm::vec4 light_position_far{0.0F};
    };

    struct alignas(16) ShadowDrawConstants {
        glm::mat4 transform{1.0F};
        uint32_t face_index = 0;
        uint32_t padding_0 = 0;
        uint32_t padding_1 = 0;
        uint32_t padding_2 = 0;
    };

    struct alignas(16) ShadowSamplingParameters {
        glm::vec4 map_parameters{0.0F};
    };

    static_assert(sizeof(GpuShadowFace) == 80);
    static_assert(sizeof(ShadowDrawConstants) == 80);
    static_assert(sizeof(ShadowSamplingParameters) == 16);

    auto checked_frame_count(uint32_t frame_count) -> uint32_t {
        if (frame_count == 0) {
            throw std::invalid_argument(
                "shadow pass requires at least one frame in flight"
            );
        }
        return frame_count;
    }

    auto checked_config(ShadowPass::Config config) -> ShadowPass::Config {
        if (config.resolution == 0) {
            throw std::invalid_argument(
                "shadow map resolution must be non-zero"
            );
        }
        if (config.max_shadow_light_count == 0) {
            throw std::invalid_argument(
                "shadow pass requires a non-zero light capacity"
            );
        }
        if (!std::isfinite(config.minimum_bias) ||
            config.minimum_bias < 0.0F) {
            throw std::invalid_argument(
                "shadow minimum bias must be finite and non-negative"
            );
        }
        if (!std::isfinite(config.maximum_filter_angle) ||
            config.maximum_filter_angle <= 0.0F) {
            throw std::invalid_argument(
                "shadow maximum filter angle must be finite and positive"
            );
        }
        if (config.max_shadow_light_count >
            std::numeric_limits<uint32_t>::max() / cube_face_count) {
            throw std::overflow_error("shadow cube layer count overflow");
        }
        return config;
    }

    auto shadow_layer_count(const ShadowPass::Config& config) -> uint32_t {
        return config.max_shadow_light_count * cube_face_count;
    }

    auto choose_depth_format(const Device& device) -> vk::Format {
        constexpr std::array candidates{
            vk::Format::eD32Sfloat,
            vk::Format::eD16Unorm
        };
        const auto required =
            vk::FormatFeatureFlagBits::eDepthStencilAttachment |
            vk::FormatFeatureFlagBits::eSampledImage;
        for (const auto format : candidates) {
            const auto properties =
                device.physical_device().getFormatProperties(format);
            if ((properties.optimalTilingFeatures & required) == required) {
                return format;
            }
        }
        throw std::runtime_error(
            "no depth format supports both attachment and sampling usage"
        );
    }

    auto validate_image_limits(
        const Device& device,
        const ShadowPass::Config& config
    ) -> void {
        const auto limits = device.physical_device().getProperties().limits;
        if (config.resolution > limits.maxImageDimensionCube) {
            throw std::out_of_range(
                "shadow map resolution exceeds maxImageDimensionCube"
            );
        }
        if (shadow_layer_count(config) > limits.maxImageArrayLayers) {
            throw std::out_of_range(
                "shadow cube array exceeds maxImageArrayLayers"
            );
        }
    }

    auto create_descriptor_set_layout(
        const Device& device,
        std::span<const vk::DescriptorSetLayoutBinding> bindings
    ) -> vk::raii::DescriptorSetLayout {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.setBindings(bindings);
        return device.logical_device().createDescriptorSetLayout(create_info);
    }

    auto create_face_descriptor_set_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment
            }
        };
        return create_descriptor_set_layout(device, bindings);
    }

    auto create_output_descriptor_set_layout(
        const Device& device
    ) -> vk::raii::DescriptorSetLayout {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            }
        };
        return create_descriptor_set_layout(device, bindings);
    }

    auto create_pipeline_layout(
        const Device& device,
        const vk::raii::DescriptorSetLayout& face_layout,
        const vk::raii::DescriptorSetLayout& material_layout
    ) -> vk::raii::PipelineLayout {
        const std::array layouts{*face_layout, *material_layout};
        const std::array push_constant_ranges{
            vk::PushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(ShadowDrawConstants)
            }
        };
        vk::PipelineLayoutCreateInfo create_info{};
        create_info
            .setSetLayouts(layouts)
            .setPushConstantRanges(push_constant_ranges);
        return device.logical_device().createPipelineLayout(create_info);
    }

    auto create_render_pass(
        const Device& device,
        vk::Format depth_format
    ) -> vk::raii::RenderPass {
        vk::AttachmentDescription depth_attachment{};
        depth_attachment
            .setFormat(depth_format)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::AttachmentReference depth_reference{};
        depth_reference
            .setAttachment(0)
            .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::SubpassDescription subpass{};
        subpass
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setPDepthStencilAttachment(&depth_reference);

        const std::array attachments{depth_attachment};
        const std::array subpasses{subpass};
        vk::RenderPassCreateInfo create_info{};
        create_info
            .setAttachments(attachments)
            .setSubpasses(subpasses);
        return device.logical_device().createRenderPass(create_info);
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
        const vk::raii::RenderPass& render_pass,
        const vk::raii::PipelineLayout& pipeline_layout
    ) -> vk::raii::Pipeline {
        const auto vertex_shader = create_shader_module(
            device,
            "./spv/shadow_vertex.spv"
        );
        const auto fragment_shader = create_shader_module(
            device,
            "./spv/shadow_fragment.spv"
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
        desc.depth_compare_op = vk::CompareOp::eLess;
        desc.color_attachment_count = 0;
        desc.depth_bias_enable = true;
        desc.depth_bias_constant_factor = 1.25F;
        desc.depth_bias_slope_factor = 1.75F;
        return GraphicsPipelineFactory::create(device, desc);
    }

    auto align_up(
        vk::DeviceSize value,
        vk::DeviceSize alignment
    ) -> vk::DeviceSize {
        if (alignment <= 1) {
            return value;
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    auto face_data_size(const ShadowPass::Config& config) -> vk::DeviceSize {
        return static_cast<vk::DeviceSize>(shadow_layer_count(config)) *
            sizeof(GpuShadowFace);
    }

    auto face_frame_stride(
        const Device& device,
        const ShadowPass::Config& config
    ) -> vk::DeviceSize {
        const auto size = face_data_size(config);
        const auto limits = device.physical_device().getProperties().limits;
        if (size > limits.maxStorageBufferRange) {
            throw std::out_of_range(
                "shadow face data exceeds maxStorageBufferRange"
            );
        }
        return align_up(size, limits.minStorageBufferOffsetAlignment);
    }

    auto create_descriptor_pool(
        const Device& device,
        vk::DescriptorType type,
        uint32_t count
    ) -> vk::raii::DescriptorPool {
        const std::array sizes{vk::DescriptorPoolSize{type, count}};
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(count)
            .setPoolSizes(sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto allocate_descriptor_sets(
        const Device& device,
        const vk::raii::DescriptorPool& pool,
        const vk::raii::DescriptorSetLayout& layout,
        uint32_t count
    ) -> std::vector<vk::raii::DescriptorSet> {
        const std::vector<vk::DescriptorSetLayout> layouts(count, *layout);
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info
            .setDescriptorPool(*pool)
            .setSetLayouts(layouts);
        return device.logical_device().allocateDescriptorSets(allocate_info);
    }

    auto create_output_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> vk::raii::DescriptorPool {
        const std::array sizes{
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampler,
                frame_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eSampledImage,
                frame_count
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer,
                frame_count
            }
        };
        vk::DescriptorPoolCreateInfo create_info{};
        create_info
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(frame_count)
            .setPoolSizes(sizes);
        return device.logical_device().createDescriptorPool(create_info);
    }

    auto create_shadow_sampler(const Device& device) -> vk::raii::Sampler {
        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(vk::Filter::eNearest)
            .setMinFilter(vk::Filter::eNearest)
            .setMipmapMode(vk::SamplerMipmapMode::eNearest)
            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
            .setAnisotropyEnable(false)
            .setCompareEnable(false)
            .setMinLod(0.0F)
            .setMaxLod(0.0F)
            .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
            .setUnnormalizedCoordinates(false);
        return device.logical_device().createSampler(create_info);
    }

    auto shadow_image_desc(
        const ShadowPass::Config& config,
        vk::Format format
    ) -> ImageDesc {
        return ImageDesc{
            .flags = vk::ImageCreateFlagBits::eCubeCompatible,
            .type = vk::ImageType::e2D,
            .format = format,
            .extent = vk::Extent3D{
                config.resolution,
                config.resolution,
                1
            },
            .mip_levels = 1,
            .array_layers = shadow_layer_count(config),
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                vk::ImageUsageFlagBits::eSampled
        };
    }

    auto create_cube_array_view(
        const Device& device,
        const Image& image,
        vk::Format format,
        uint32_t layer_count
    ) -> vk::raii::ImageView {
        vk::ImageViewCreateInfo create_info{};
        create_info
            .setImage(image.get())
            .setViewType(vk::ImageViewType::eCubeArray)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eDepth,
                0,
                1,
                0,
                layer_count
            });
        return device.logical_device().createImageView(create_info);
    }

    auto create_face_views(
        const Device& device,
        const Image& image,
        vk::Format format,
        uint32_t layer_count
    ) -> std::vector<vk::raii::ImageView> {
        std::vector<vk::raii::ImageView> views;
        views.reserve(layer_count);
        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            vk::ImageViewCreateInfo create_info{};
            create_info
                .setImage(image.get())
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setSubresourceRange(vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eDepth,
                    0,
                    1,
                    layer,
                    1
                });
            views.emplace_back(
                device.logical_device().createImageView(create_info)
            );
        }
        return views;
    }

    auto create_framebuffers(
        const Device& device,
        const vk::raii::RenderPass& render_pass,
        const std::vector<vk::raii::ImageView>& face_views,
        uint32_t resolution
    ) -> std::vector<vk::raii::Framebuffer> {
        std::vector<vk::raii::Framebuffer> framebuffers;
        framebuffers.reserve(face_views.size());
        for (const auto& face_view : face_views) {
            const std::array attachments{*face_view};
            vk::FramebufferCreateInfo create_info{};
            create_info
                .setRenderPass(*render_pass)
                .setAttachments(attachments)
                .setWidth(resolution)
                .setHeight(resolution)
                .setLayers(1);
            framebuffers.emplace_back(
                device.logical_device().createFramebuffer(create_info)
            );
        }
        return framebuffers;
    }

    auto shadow_projection(float near_plane, float far_plane) -> glm::mat4 {
        return glm::perspectiveRH_ZO(
            glm::half_pi<float>(),
            1.0F,
            near_plane,
            far_plane
        );
    }

    auto shadow_view(
        const glm::vec3& position,
        uint32_t face
    ) -> glm::mat4 {
        constexpr std::array directions{
            glm::vec3{1.0F, 0.0F, 0.0F},
            glm::vec3{-1.0F, 0.0F, 0.0F},
            glm::vec3{0.0F, 1.0F, 0.0F},
            glm::vec3{0.0F, -1.0F, 0.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
            glm::vec3{0.0F, 0.0F, -1.0F}
        };
        constexpr std::array up_vectors{
            glm::vec3{0.0F, -1.0F, 0.0F},
            glm::vec3{0.0F, -1.0F, 0.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
            glm::vec3{0.0F, 0.0F, -1.0F},
            glm::vec3{0.0F, -1.0F, 0.0F},
            glm::vec3{0.0F, -1.0F, 0.0F}
        };
        return glm::lookAtRH(
            position,
            position + directions.at(face),
            up_vectors.at(face)
        );
    }

    auto validate_shadow_light(const PointLight& light) -> void {
        if (!std::isfinite(light.shadow_near) ||
            light.shadow_near <= 0.0F) {
            throw std::invalid_argument(
                "point light shadow_near must be finite and positive"
            );
        }
        if (!std::isfinite(light.shadow_far) ||
            light.shadow_far <= light.shadow_near) {
            throw std::invalid_argument(
                "point light shadow_far must exceed shadow_near"
            );
        }
        if (!std::isfinite(light.source_radius) ||
            light.source_radius < 0.0F) {
            throw std::invalid_argument(
                "point light source_radius must be finite and non-negative"
            );
        }
    }

    auto shadow_subresource_range(
        uint32_t layer_count
    ) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eDepth,
            0,
            1,
            0,
            layer_count
        };
    }

    auto transition_shadow_image(
        vk::raii::CommandBuffer& command_buffer,
        vk::Image image,
        uint32_t layer_count,
        bool initialized,
        bool to_attachment
    ) -> void {
        const auto old_layout = to_attachment
            ? (initialized
                ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                : vk::ImageLayout::eUndefined)
            : vk::ImageLayout::eDepthStencilAttachmentOptimal;
        const auto new_layout = to_attachment
            ? vk::ImageLayout::eDepthStencilAttachmentOptimal
            : vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        const auto source_access = to_attachment
            ? (initialized
                ? vk::AccessFlags{vk::AccessFlagBits::eShaderRead}
                : vk::AccessFlags{})
            : vk::AccessFlags{vk::AccessFlagBits::eDepthStencilAttachmentWrite};
        const auto destination_access = to_attachment
            ? vk::AccessFlags{
                vk::AccessFlagBits::eDepthStencilAttachmentRead |
                vk::AccessFlagBits::eDepthStencilAttachmentWrite
            }
            : vk::AccessFlags{vk::AccessFlagBits::eShaderRead};
        const auto source_stage = to_attachment
            ? (initialized
                ? vk::PipelineStageFlags{
                    vk::PipelineStageFlagBits::eFragmentShader
                }
                : vk::PipelineStageFlags{
                    vk::PipelineStageFlagBits::eTopOfPipe
                })
            : vk::PipelineStageFlags{
                vk::PipelineStageFlagBits::eEarlyFragmentTests |
                vk::PipelineStageFlagBits::eLateFragmentTests
            };
        const auto destination_stage = to_attachment
            ? vk::PipelineStageFlags{
                vk::PipelineStageFlagBits::eEarlyFragmentTests |
                vk::PipelineStageFlagBits::eLateFragmentTests
            }
            : vk::PipelineStageFlags{
                vk::PipelineStageFlagBits::eFragmentShader
            };

        vk::ImageMemoryBarrier barrier{};
        barrier
            .setSrcAccessMask(source_access)
            .setDstAccessMask(destination_access)
            .setOldLayout(old_layout)
            .setNewLayout(new_layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(shadow_subresource_range(layer_count));
        command_buffer.pipelineBarrier(
            source_stage,
            destination_stage,
            {},
            {},
            {},
            barrier
        );
    }
}

struct ShadowPass::FrameResources {
    FrameResources(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::RenderPass& render_pass,
        const Config& config,
        vk::Format depth_format
    ) : image(allocator, shadow_image_desc(config, depth_format)),
        cube_array_view(
            create_cube_array_view(
                device,
                image,
                depth_format,
                shadow_layer_count(config)
            )
        ),
        face_views(
            create_face_views(
                device,
                image,
                depth_format,
                shadow_layer_count(config)
            )
        ),
        framebuffers(
            create_framebuffers(
                device,
                render_pass,
                face_views,
                config.resolution
            )
        ) {}

    Image image;
    vk::raii::ImageView cube_array_view = nullptr;
    std::vector<vk::raii::ImageView> face_views;
    std::vector<vk::raii::Framebuffer> framebuffers;
    bool initialized = false;
};

ShadowPass::ShadowPass(
    const Device& device,
    const MemoryAllocator& allocator,
    uint32_t frame_count,
    Config config
) : config_(checked_config(config)) {
    const auto checked_frames = checked_frame_count(frame_count);
    validate_image_limits(device, config_);
    depth_format_ = choose_depth_format(device);
    face_descriptor_set_layout_ = create_face_descriptor_set_layout(device);
    output_descriptor_set_layout_ =
        create_output_descriptor_set_layout(device);
    material_writer_ = std::make_unique<ShadowMaterialWriter>(
        device,
        allocator
    );
    pipeline_layout_ = create_pipeline_layout(
        device,
        face_descriptor_set_layout_,
        material_writer_->descriptor_set_layout()
    );
    render_pass_ = create_render_pass(device, depth_format_);
    pipeline_ = create_pipeline(device, render_pass_, pipeline_layout_);

    face_frame_stride_ = face_frame_stride(device, config_);
    face_buffer_ = Buffer(
        allocator,
        BufferDesc{
            .size = face_frame_stride_ * checked_frames,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }
    );
    sampling_parameter_buffer_ = Buffer(
        allocator,
        BufferDesc{
            .size = sizeof(ShadowSamplingParameters),
            .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memory = BufferMemoryUsage::Upload,
            .persistent_mapping = true
        }
    );
    const ShadowSamplingParameters sampling_parameters{
        .map_parameters = glm::vec4{
            static_cast<float>(config_.resolution),
            1.0F / static_cast<float>(config_.resolution),
            config_.minimum_bias,
            config_.maximum_filter_angle
        }
    };
    sampling_parameter_buffer_.write(
        &sampling_parameters,
        sizeof(sampling_parameters)
    );

    face_descriptor_pool_ = create_descriptor_pool(
        device,
        vk::DescriptorType::eStorageBuffer,
        checked_frames
    );
    face_descriptor_sets_ = allocate_descriptor_sets(
        device,
        face_descriptor_pool_,
        face_descriptor_set_layout_,
        checked_frames
    );
    for (uint32_t frame_index = 0;
        frame_index < checked_frames;
        ++frame_index) {
        const vk::DescriptorBufferInfo buffer_info{
            .buffer = face_buffer_.get(),
            .offset = face_frame_stride_ * frame_index,
            .range = face_data_size(config_)
        };
        const vk::WriteDescriptorSet write = vk::WriteDescriptorSet{}
            .setDstSet(*face_descriptor_sets_[frame_index])
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(buffer_info);
        device.logical_device().updateDescriptorSets(write, {});
    }

    output_descriptor_pool_ = create_output_descriptor_pool(
        device,
        checked_frames
    );
    output_descriptor_sets_ = allocate_descriptor_sets(
        device,
        output_descriptor_pool_,
        output_descriptor_set_layout_,
        checked_frames
    );
    shadow_sampler_ = create_shadow_sampler(device);

    frame_resources_.reserve(checked_frames);
    light_bindings_.resize(checked_frames);
    for (uint32_t frame_index = 0;
        frame_index < checked_frames;
        ++frame_index) {
        frame_resources_.push_back(
            std::make_unique<FrameResources>(
                device,
                allocator,
                render_pass_,
                config_,
                depth_format_
            )
        );
        const vk::DescriptorImageInfo sampler_info{
            .sampler = *shadow_sampler_
        };
        const vk::DescriptorImageInfo image_info{
            .imageView = *frame_resources_.back()->cube_array_view,
            .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal
        };
        const vk::DescriptorBufferInfo parameter_info{
            .buffer = sampling_parameter_buffer_.get(),
            .offset = 0,
            .range = sizeof(ShadowSamplingParameters)
        };
        const std::array writes{
            vk::WriteDescriptorSet{}
                .setDstSet(*output_descriptor_sets_[frame_index])
                .setDstBinding(0)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setImageInfo(sampler_info),
            vk::WriteDescriptorSet{}
                .setDstSet(*output_descriptor_sets_[frame_index])
                .setDstBinding(1)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setImageInfo(image_info),
            vk::WriteDescriptorSet{}
                .setDstSet(*output_descriptor_sets_[frame_index])
                .setDstBinding(2)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setBufferInfo(parameter_info)
        };
        device.logical_device().updateDescriptorSets(writes, {});
    }
}

ShadowPass::~ShadowPass() = default;

auto ShadowPass::write_material(
    std::span<const ResourceId<Material>> material_ids,
    const ResourceRegistry& registry
) -> void {
    for (const auto material_id : material_ids) {
        material_writer_->write(
            material_id,
            registry.query(material_id)
        );
    }
}

auto ShadowPass::record(
    ExecutionContext context,
    Input input
) -> Output {
    auto& frame = *frame_resources_.at(context.frame_index);
    auto& bindings = light_bindings_.at(context.frame_index);
    const auto& lights = input.scene.point_lights();
    bindings.assign(lights.size(), PointLightShadowBinding{});

    std::vector<GpuShadowFace> faces(
        shadow_layer_count(config_),
        GpuShadowFace{}
    );
    uint32_t shadow_light_count = 0;
    for (std::size_t light_index = 0;
        light_index < lights.size();
        ++light_index) {
        const auto& light = lights[light_index];
        if (!light.casts_shadow ||
            shadow_light_count >= config_.max_shadow_light_count) {
            continue;
        }
        validate_shadow_light(light);
        bindings[light_index] = PointLightShadowBinding{
            .shadow_index = static_cast<int32_t>(shadow_light_count),
            .near_plane = light.shadow_near,
            .far_plane = light.shadow_far,
            .source_radius = light.source_radius
        };

        const auto projection = shadow_projection(
            light.shadow_near,
            light.shadow_far
        );
        for (uint32_t face = 0; face < cube_face_count; ++face) {
            const auto face_index =
                shadow_light_count * cube_face_count + face;
            faces[face_index] = GpuShadowFace{
                .light_view_projection = projection * shadow_view(
                    light.position,
                    face
                ),
                .light_position_far = glm::vec4{
                    light.position,
                    light.shadow_far
                }
            };
        }
        ++shadow_light_count;
    }

    face_buffer_.write(
        faces.data(),
        face_data_size(config_),
        face_frame_stride_ * context.frame_index
    );

    auto& command_buffer = context.command_buffer;
    transition_shadow_image(
        command_buffer,
        frame.image.get(),
        shadow_layer_count(config_),
        frame.initialized,
        true
    );

    const vk::Viewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(config_.resolution),
        .height = static_cast<float>(config_.resolution),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };
    const vk::Rect2D scissor{
        {0, 0},
        {config_.resolution, config_.resolution}
    };
    vk::ClearValue clear_value{};
    clear_value.depthStencil = vk::ClearDepthStencilValue{1.0F, 0};
    const std::array face_descriptor_sets{
        *face_descriptor_sets_.at(context.frame_index)
    };

    for (uint32_t shadow_index = 0;
        shadow_index < shadow_light_count;
        ++shadow_index) {
        for (uint32_t face = 0; face < cube_face_count; ++face) {
            const auto face_index =
                shadow_index * cube_face_count + face;
            vk::RenderPassBeginInfo begin_info{};
            begin_info
                .setRenderPass(*render_pass_)
                .setFramebuffer(*frame.framebuffers.at(face_index))
                .setRenderArea(scissor)
                .setClearValues(clear_value);
            command_buffer.beginRenderPass(
                begin_info,
                vk::SubpassContents::eInline
            );
            command_buffer.setViewport(0, viewport);
            command_buffer.setScissor(0, scissor);
            command_buffer.bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                *pipeline_
            );
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pipeline_layout_,
                0,
                face_descriptor_sets,
                {}
            );

            for (const auto& entity : input.scene.entities()) {
                command_buffer.pushConstants<ShadowDrawConstants>(
                    *pipeline_layout_,
                    vk::ShaderStageFlagBits::eVertex |
                        vk::ShaderStageFlagBits::eFragment,
                    0,
                    ShadowDrawConstants{
                        .transform = entity.model_matrix(),
                        .face_index = face_index
                    }
                );
                const auto& model =
                    input.registry.query(entity.model_id());
                for (const auto& mesh : model.meshes()) {
                    const std::array material_descriptor_sets{
                        *material_writer_->descriptor_set(mesh.material())
                    };
                    command_buffer.bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics,
                        *pipeline_layout_,
                        1,
                        material_descriptor_sets,
                        {}
                    );
                    mesh.bind(command_buffer);
                    command_buffer.drawIndexed(
                        mesh.index_count(),
                        1,
                        0,
                        0,
                        0
                    );
                }
            }
            command_buffer.endRenderPass();
        }
    }

    transition_shadow_image(
        command_buffer,
        frame.image.get(),
        shadow_layer_count(config_),
        true,
        false
    );
    frame.initialized = true;

    return Output{
        .descriptor_set = *output_descriptor_sets_.at(context.frame_index),
        .light_bindings = bindings,
        .shadow_light_count = shadow_light_count
    };
}
