#include "render/renderer.hpp"
#include "render/pipeline/basic_rendering.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
    auto create_descriptor_pool(
        const Device& device,
        uint32_t frame_count
    ) -> DescriptorPool {
        vk::DescriptorPoolSize camera_uniform_pool_size{};
        camera_uniform_pool_size
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(frame_count);

        const std::array pool_sizes{camera_uniform_pool_size};
        return DescriptorPool(device, frame_count, pool_sizes);
    }
}

Renderer::Renderer(const Window& window)
    : device_context_(window),
      data_uploader_(
          device_context_.device(),
          device_context_.allocator()
      ),
      swapchain_context_(device_context_, window),
      frame_context_(device_context_.device()),
      descriptor_pool_(
        create_descriptor_pool(
          device_context_.device(),
          frame_context_.frame_count()
        )),
      main_pipeline_(BasicRenderingPipelineFactory::create(
          device_context_.device(),
          swapchain_context_.render_pass()
      )),
      camera_uniforms_context_(
        device_context_.device(),
        device_context_.allocator(),
        descriptor_pool_,
        main_pipeline_.descriptor_set_layout(0),
        frame_context_
      ) {
    create_mesh();
}

auto Renderer::create_mesh() -> void {
    mesh_.vertices_ = {
        Vertex{{0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.7F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.1571F, 0.2162F}, {1.0F, 0.5F, 0.0F}},
        Vertex{{-0.6658F, 0.2163F}, {1.0F, 1.0F, 0.0F}},
        Vertex{{-0.2542F, -0.0826F}, {0.5F, 1.0F, 0.0F}},
        Vertex{{-0.4115F, -0.5663F}, {0.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, -0.2673F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.4115F, -0.5663F}, {1.0F, 0.0F, 1.0F}},
        Vertex{{0.2542F, -0.0826F}, {1.0F, 0.5F, 0.5F}},
        Vertex{{0.6658F, 0.2163F}, {0.8F, 0.0F, 0.5F}},
        Vertex{{0.1571F, 0.2162F}, {0.0F, 0.5F, 0.5F}}
    };

    mesh_.indices_ = std::vector<uint32_t> {
        0, 2, 1,
        0, 3, 2,
        0, 4, 3,
        0, 5, 4,
        0, 6, 5,
        0, 7, 6,
        0, 8, 7,
        0, 9, 8,
        0, 10, 9,
        0, 1, 10
    };
    render_mesh_ = RenderMesh(
        mesh_,
        device_context_.allocator(),
        data_uploader_
    );
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
        vk::ClearValue clear_value{};
        clear_value.color.float32[0] = 0.02F;
        clear_value.color.float32[1] = 0.02F;
        clear_value.color.float32[2] = 0.03F;
        clear_value.color.float32[3] = 1.0F;

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info
            .setRenderPass(swapchain_context_.render_pass().get())
            .setFramebuffer(swapchain_context_.framebuffers().at(image_id).get())
            .setRenderArea({.offset = {0, 0},
                            .extent = swapchain_context_.swapchain().swapchain_image_extent()})
            .setClearValues(clear_value);

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

        const auto camera_descriptor_set = *camera_uniforms_context_.current_descriptor_set();
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *main_pipeline_.layout().get(),
            0,
            camera_descriptor_set,
            {}
        );

        render_mesh_.bind(cmd);
        cmd.drawIndexed(render_mesh_.index_count(), 1, 0, 0, 0);
        cmd.endRenderPass();
    };

    frame_context_.current_command_context().record(cmd);
}

auto Renderer::submit(uint32_t image_id) -> void {
    auto& device = device_context_.device();

    const auto wait_semaphore = *frame_context_.current_image_available();
    const auto wait_stage = vk::PipelineStageFlags{
        vk::PipelineStageFlagBits::eColorAttachmentOutput};
    const auto command_buffer = *frame_context_
                                     .current_command_context()
                                     .command_buffers()[0];
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
        const auto result = device_context_.device()
                                .present_queue()
                                .presentKHR(present_info);

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
