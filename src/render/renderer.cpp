#include "render/renderer.hpp"
#include "render/pipeline/basic_rendering.hpp"
#include "io/image_loader.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
    auto create_default_mesh_data() -> MeshData {
        return MeshData{
            .vertices_ = {
                // +Z
                Vertex{{-0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{-0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},

                // -Z
                Vertex{{0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{-0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{-0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},

                // -X
                Vertex{{-0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{-0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{-0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{-0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},

                // +X
                Vertex{{0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},

                // +Y
                Vertex{{-0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{-0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},

                // -Y
                Vertex{{-0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
                Vertex{{0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},
                Vertex{{0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}},
                Vertex{{-0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}}
            },
            .indices_ = {
                0, 1, 2, 0, 2, 3,
                4, 5, 6, 4, 6, 7,
                8, 9, 10, 8, 10, 11,
                12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19,
                20, 21, 22, 20, 22, 23
            }
        };
    }

    auto create_default_texture(
        DeviceContext& device_context
    ) -> Texture2D {
        const auto image_data = load_image_rgba8(
            "./assets/texture/shinku.jpg"
        );

        return Texture2D{
            device_context.device(),
            device_context.allocator(),
            device_context.uploader(),
            image_data.width,
            image_data.height,
            image_data.pixels,
            vk::Format::eR8G8B8A8Srgb
        };
    }

    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count) -> DescriptorPool {
        vk::DescriptorPoolSize camera_uniform_pool_size{};
        camera_uniform_pool_size
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(frame_count);

        vk::DescriptorPoolSize sampled_image_pool_size{};
        sampled_image_pool_size
            .setType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1);

        vk::DescriptorPoolSize sampler_pool_size{};
        sampler_pool_size
            .setType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1);

        const std::array pool_sizes{
            camera_uniform_pool_size,
            sampled_image_pool_size,
            sampler_pool_size
        };
        return DescriptorPool(
            device,
            frame_count + 1,
            pool_sizes
        );
    }
}

Renderer::Renderer(const Window& window) : 
    device_context_(window),
    swapchain_context_(device_context_, window),
    frame_context_(device_context_.device()),
    descriptor_pool_(
        create_descriptor_pool(
            device_context_.device(),
            frame_context_.frame_count()
        )
    ),
    main_pipeline_(
        BasicRenderingPipelineFactory::create(
        device_context_.device(),
        swapchain_context_.render_pass())
    ),
    uniforms_context_(
        device_context_.device(),
        device_context_.allocator(),
        descriptor_pool_,
        main_pipeline_.descriptor_set_layout(0),
        frame_context_),
    mesh_data_(
        create_default_mesh_data()
    ),
    mesh_(
        mesh_data_,
        device_context_.allocator(),
        device_context_.uploader()
    ),
    texture_(
        create_default_texture(device_context_)
    ),
    sampler_(
        device_context_.device(), 
        SamplerDesc{}
    ),
    material_context_(
        device_context_.device(),
        descriptor_pool_,
        main_pipeline_.descriptor_set_layout(1),
        texture_,
        sampler_
    ) {}

auto Renderer::update_ubo() -> void {
    static auto startTime = std::chrono::high_resolution_clock::now();
    const auto currentTime = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    UniformBufferObject ubo{};
    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        time * glm::radians(90.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
    ubo.view = glm::lookAt(
        glm::vec3(2.0f, 2.0f, 2.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(
            swapchain_context_.swapchain().swapchain_image_extent().width
        ) /
        static_cast<float>(
            swapchain_context_.swapchain().swapchain_image_extent().height
        ),
        0.1f,
        20.0f
    );
    ubo.projection[1][1] *= -1;
    uniforms_context_.update(ubo);
}

auto Renderer::render() -> void {
    auto& device = device_context_.device();
    frame_context_.wait(device);

    auto& image_available = frame_context_.current_image_available();
    uint32_t image_id = 0;
    bool swapchain_suboptimal = false;

    const auto acquire_result = swapchain_context_.acquire(image_available);
    image_id = acquire_result.value;
    swapchain_suboptimal = acquire_result.result == vk::Result::eSuboptimalKHR;

    update_ubo();
    record(image_id);
    submit(image_id);
    const auto present_result = present(image_id);

    frame_context_.advance();

    if (swapchain_suboptimal ||
        present_result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eErrorOutOfDateKHR) {
        throw vk::OutOfDateKHRError("swapchain recreation required!");
    }
}

auto Renderer::record(uint32_t image_id) -> void {
    auto cmd = [&](vk::raii::CommandBuffer& cmd) {
        std::array<vk::ClearValue, 2> clear_values{};
        clear_values[0].color.float32[0] = 0.02F;
        clear_values[0].color.float32[1] = 0.02F;
        clear_values[0].color.float32[2] = 0.03F;
        clear_values[0].color.float32[3] = 1.0F;
        clear_values[1].depthStencil.depth = 1.0F;
        clear_values[1].depthStencil.stencil = 0;

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info
            .setRenderPass(swapchain_context_.render_pass().get())
            .setFramebuffer(swapchain_context_.framebuffers().at(image_id).get())
            .setRenderArea({.offset = {0, 0},
                            .extent = swapchain_context_.swapchain().swapchain_image_extent()})
            .setClearValues(clear_values);

        cmd.beginRenderPass(
            render_pass_info,
            vk::SubpassContents::eInline);

        const auto extent = swapchain_context_
                                .swapchain()
                                .swapchain_image_extent();

        vk::Viewport viewport{};
        viewport
            .setX(0.0F)
            .setY(0.0F)
            .setWidth(static_cast<float>(extent.width))
            .setHeight(static_cast<float>(extent.height))
            .setMinDepth(0.0F)
            .setMaxDepth(1.0F);

        vk::Rect2D scissor{};
        scissor
            .setOffset({0, 0})
            .setExtent(extent);

        cmd.setViewport(0, viewport);
        cmd.setScissor(0, scissor);
        main_pipeline_.bind(cmd);

        const std::array descriptor_sets{
            *uniforms_context_.current_descriptor_set(),
            *material_context_.descriptor_set()
        };
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *main_pipeline_.layout().get(),
            0,
            descriptor_sets,
            {});

        mesh_.bind(cmd);
        cmd.drawIndexed(mesh_.index_count(), 1, 0, 0, 0);
        cmd.endRenderPass();
    };

    frame_context_.current_command_context().record(cmd);
}

auto Renderer::submit(uint32_t image_id) -> void {
    auto& device = device_context_.device();

    const auto wait_semaphore = *frame_context_.current_image_available();
    const auto wait_stage =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests |
        vk::PipelineStageFlagBits::eLateFragmentTests;
    const auto command_buffer = *frame_context_.current_command_context().command_buffers()[0];
    const auto signal_semaphore = *swapchain_context_.render_finished(image_id);

    vk::SubmitInfo submit_info{};
    submit_info
        .setWaitSemaphores(wait_semaphore)
        .setWaitDstStageMask(wait_stage)
        .setCommandBuffers(command_buffer)
        .setSignalSemaphores(signal_semaphore);

    frame_context_.reset(device);
    device.graphics_queue().submit(
        submit_info,
        frame_context_.current_in_flight_fence());
}

auto Renderer::present(uint32_t image_id) -> vk::Result {
    const auto wait_semaphore = *swapchain_context_.render_finished(image_id);
    const auto swapchain = *swapchain_context_.swapchain().get();

    vk::PresentInfoKHR present_info{};
    present_info
        .setWaitSemaphores(wait_semaphore)
        .setSwapchains(swapchain)
        .setImageIndices(image_id);

    try {
        const auto result = device_context_.device().present_queue().presentKHR(present_info);
        if (result != vk::Result::eSuccess &&
            result != vk::Result::eSuboptimalKHR &&
            result != vk::Result::eErrorOutOfDateKHR) {
            throw std::runtime_error("failed to present swapchain image!");
        }
        return result;
    } 
    catch (const vk::OutOfDateKHRError&) {
        return vk::Result::eErrorOutOfDateKHR;
    }
}

auto Renderer::recreate_swapchain(const Window& window) -> void {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window.get(), &width, &height);

    while ((width == 0 || height == 0) && !window.should_close()) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window.get(), &width, &height);
    }

    if (window.should_close()) {
        return;
    }

    auto& device = device_context_.device();
    device.logical_device().waitIdle();

    auto old_swapchain_context = std::move(swapchain_context_);
    const auto old_swapchain = *old_swapchain_context.swapchain().get();

    auto replacement = SwapchainContext(
        device_context_,
        window,
        old_swapchain
    );
    swapchain_context_ = std::move(replacement);
    main_pipeline_ = BasicRenderingPipelineFactory::create(
        device,
        swapchain_context_.render_pass()
    );
}
