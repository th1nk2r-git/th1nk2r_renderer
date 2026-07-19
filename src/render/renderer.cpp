#include "render/renderer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <utility>

auto Renderer::create(const Window& window) -> void {
    device_context_.create(window);
    data_uploader_ = DataUploader(
        device_context_.device(),
        device_context_.allocator()
    );
    swapchain_context_.create(device_context_, window);
    frame_context_.create(device_context_, swapchain_context_);
    main_pipeline_layout_ = PipelineLayout(device_context_.device());
    create_main_pipeline();
    create_buffers();
}

auto Renderer::create_main_pipeline() -> void {
    const auto& device = device_context_.device();
    const ShaderModule vertex_shader{
        device,
        "./spv/vertex.spv"
    };
    const ShaderModule fragment_shader{
        device,
        "./spv/fragment.spv"
    };

    vk::VertexInputBindingDescription vertex_binding{};
    vertex_binding
        .setBinding(0)
        .setStride(sizeof(Vertex))
        .setInputRate(vk::VertexInputRate::eVertex);

    vk::VertexInputAttributeDescription position_attribute{};
    position_attribute
        .setLocation(0)
        .setBinding(0)
        .setFormat(vk::Format::eR32G32Sfloat)
        .setOffset(offsetof(Vertex, position));

    vk::VertexInputAttributeDescription color_attribute{};
    color_attribute
        .setLocation(1)
        .setBinding(0)
        .setFormat(vk::Format::eR32G32B32Sfloat)
        .setOffset(offsetof(Vertex, color));

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.vertex_shader = &vertex_shader;
    pipeline_desc.fragment_shader = &fragment_shader;
    pipeline_desc.layout = &main_pipeline_layout_;
    pipeline_desc.render_pass = &swapchain_context_.render_pass();
    pipeline_desc.vertex_bindings.push_back(vertex_binding);
    pipeline_desc.vertex_attributes = {
        position_attribute,
        color_attribute
    };

    main_pipeline_ = GraphicsPipeline(device, pipeline_desc);
}

auto Renderer::create_buffers() -> void {
    const vk::DeviceSize vertices_size = sizeof(Vertex) * vertices_.size();

    const vk::DeviceSize indices_size  = sizeof(uint16_t) * indices_.size();

    vertex_buffer_ = Buffer {
        device_context_.allocator(),
        BufferDesc{
            .size = vertices_size,
            .usage = vk::BufferUsageFlagBits::eVertexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
            .memory = BufferMemoryUsage::GpuOnly
        }
    };

    index_buffer_ = Buffer {
        device_context_.allocator(),
        BufferDesc{
            .size = indices_size,
            .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
            .memory = BufferMemoryUsage::GpuOnly
        }
    };

    data_uploader_.enqueue(
        vertices_.data(),
        vertices_size,
        vertex_buffer_,
        vk::PipelineStageFlagBits::eVertexInput,
        vk::AccessFlagBits::eVertexAttributeRead
    );
    data_uploader_.enqueue(
        indices_.data(),
        indices_size,
        index_buffer_,
        vk::PipelineStageFlagBits::eVertexInput,
        vk::AccessFlagBits::eIndexRead
    );
    data_uploader_.submit_and_wait();
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
        cmd.bindPipeline(
            vk::PipelineBindPoint::eGraphics,
            main_pipeline_.get()
        );

        const std::array vertex_buffers {
            vertex_buffer_.get()
        };

        constexpr std::array<vk::DeviceSize, 1> offsets{0};

        cmd.bindVertexBuffers(0, vertex_buffers, offsets);
        cmd.bindIndexBuffer(index_buffer_.get(), 0, vk::IndexType::eUint16);

        cmd.drawIndexed(indices_.size(), 1, 0, 0, 0);
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
    const auto signal_semaphore = *frame_context_.render_finished(image_id);

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
    const auto wait_semaphore = *frame_context_.render_finished(image_id);
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

    swapchain_context_.create(
        device_context_,
        window,
        old_swapchain
    );
    create_main_pipeline();
    frame_context_.recreate_swapchain_resources(
        device,
        swapchain_context_
    );
}
