#include "render/renderer.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <utility>

Renderer::Renderer(
    DeviceContext& device_context,
    Window& window,
    const ResourceRegistry& resources,
    ThreadPool& thread_pool
) : device_context_(device_context),
    window_(window),
    swapchain_(device_context, window),
    frames_in_flight_(device_context.device()),
    forward_pass_(
        device_context.device(),
        device_context.allocator(),
        frames_in_flight_,
        swapchain_,
        resources
    ),
    render_graph_(images_, buffers_, thread_pool) {}

auto Renderer::init() -> void {
    if (initialized_) {
        throw std::logic_error(
            "renderer is already initialized!"
        );
    }

    init_render_pass();
    build_render_graph();
    initialized_ = true;
}

auto Renderer::init_render_pass() -> void {
    forward_pass_.init();
}

auto Renderer::build_render_graph() -> void {
    render_graph_.create_node(
        "forward",
        [this] {
            return forward_pass_.record();
        }
    );
    render_graph_.compile();
}

Renderer::~Renderer() noexcept {
    try {
        wait_idle();
    }
    catch (...) {
    }
}

auto Renderer::wait_idle() const -> void {
    device_context_.device().logical_device().waitIdle();
}

auto Renderer::recreate_swapchain() -> void {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_.get(), &width, &height);

    while ((width == 0 || height == 0) && !window_.should_close()) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_.get(), &width, &height);
    }

    if (window_.should_close()) {
        return;
    }

    wait_idle();
    auto replacement = Swapchain(
        device_context_,
        window_,
        *swapchain_.handle()
    );
    if (!swapchain_.compatible_with(replacement)) {
        forward_pass_.recreate_pipeline(replacement.render_pass());
    }
    swapchain_ = std::move(replacement);
    static_cast<void>(window_.consume_framebuffer_resized());
}

auto Renderer::record_frame(
    const Scene& scene,
    uint32_t image_index
) -> void {
    auto& frame = frames_in_flight_.current();
    frame.reset_primary();
    forward_pass_.prepare(scene, image_index);

    auto& command_buffer = frame.primary_command_buffer;
    command_buffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color.float32[0] = 0.01F;
    clear_values[0].color.float32[1] = 0.015F;
    clear_values[0].color.float32[2] = 0.025F;
    clear_values[0].color.float32[3] = 1.0F;
    clear_values[1].depthStencil.depth = 1.0F;
    clear_values[1].depthStencil.stencil = 0;

    vk::RenderPassBeginInfo begin_info{};
    begin_info
        .setRenderPass(*swapchain_.render_pass())
        .setFramebuffer(*swapchain_.framebuffers().at(image_index))
        .setRenderArea(vk::Rect2D{
            .offset = vk::Offset2D{0, 0},
            .extent = swapchain_.extent()
        })
        .setClearValues(clear_values);

    command_buffer.beginRenderPass(
        begin_info,
        vk::SubpassContents::eSecondaryCommandBuffers
    );
    render_graph_.record(command_buffer);
    command_buffer.endRenderPass();
    command_buffer.end();
}

auto Renderer::submit(uint32_t image_index) -> void {
    const auto& device = device_context_.device();
    auto& frame = frames_in_flight_.current();

    const auto wait_semaphore = *frame.image_available;
    const vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const auto command_buffer = *frame.primary_command_buffer;
    const auto signal_semaphore = *swapchain_.render_finished(image_index);

    vk::SubmitInfo submit_info{};
    submit_info
        .setWaitSemaphores(wait_semaphore)
        .setWaitDstStageMask(wait_stage)
        .setCommandBuffers(command_buffer)
        .setSignalSemaphores(signal_semaphore);

    frames_in_flight_.reset_current_fence();
    device.graphics_queue().submit(
        submit_info,
        frame.in_flight_fence
    );
}

auto Renderer::present(uint32_t image_index) -> vk::Result {
    const auto wait_semaphore = *swapchain_.render_finished(image_index);
    const auto swapchain = *swapchain_.handle();

    vk::PresentInfoKHR present_info{};
    present_info
        .setWaitSemaphores(wait_semaphore)
        .setSwapchains(swapchain)
        .setImageIndices(image_index);

    try {
        const auto result =
            device_context_.device().present_queue().presentKHR(
                present_info
            );
        if (result != vk::Result::eSuccess &&
            result != vk::Result::eSuboptimalKHR &&
            result != vk::Result::eErrorOutOfDateKHR) {
            throw std::runtime_error(
                "failed to present swapchain image!"
            );
        }
        return result;
    }
    catch (const vk::OutOfDateKHRError&) {
        return vk::Result::eErrorOutOfDateKHR;
    }
}

auto Renderer::render(const Scene& scene) -> FrameResult {
    if (!initialized_) {
        throw std::logic_error(
            "renderer must be initialized before rendering!"
        );
    }

    if (window_.should_close()) {
        return FrameResult::Skipped;
    }
    if (window_.consume_framebuffer_resized()) {
        recreate_swapchain();
        return FrameResult::Skipped;
    }

    frames_in_flight_.wait_current();

    std::optional<vk::ResultValue<uint32_t>> acquired;
    try {
        acquired = swapchain_.acquire(
            frames_in_flight_.current().image_available
        );
    }
    catch (const vk::OutOfDateKHRError&) {
        recreate_swapchain();
        return FrameResult::Skipped;
    }

    const auto image_index = acquired->value;
    record_frame(scene, image_index);
    submit(image_index);
    const auto present_result = present(image_index);
    frames_in_flight_.advance();

    if (acquired->result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eErrorOutOfDateKHR) {
        recreate_swapchain();
        return FrameResult::Skipped;
    }
    return FrameResult::Rendered;
}
