#include "render/renderer.hpp"

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
    resources_(resources),
    swapchain_(device_context, window),
    frames_in_flight_(device_context.device()),
    render_graph_(images_, buffers_, thread_pool) {
    bind_swapchain_images(0);
}

auto Renderer::init() -> void {
    create_render_pass();
    init_render_pass();
    build_render_graph();
}

auto Renderer::create_render_pass() -> void {
}

auto Renderer::init_render_pass() -> void {
    for (auto& render_pass : render_passes_) {
        render_pass->init();
    }
}

auto Renderer::build_render_graph() -> void {
    for (const auto& render_pass : render_passes_) {
        auto* pass = render_pass.get();
        render_graph_.create_node(
            std::string{pass->name()},
            [pass] {
                return pass->record();
            }
        );
    }

    for (const auto& render_pass : render_passes_) {
        render_pass->configure(render_graph_);
    }

    render_graph_.set_output("backbuffer");
    render_graph_.compile();
}

auto Renderer::bind_swapchain_images(uint32_t image_index) -> void {
    images_.bind_external(
        "backbuffer",
        swapchain_.image(image_index)
    );
    images_.bind_external(
        "depth",
        swapchain_.depth_image(image_index)
    );
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
    swapchain_ = std::move(replacement);
    bind_swapchain_images(0);
    static_cast<void>(window_.consume_framebuffer_resized());
}

auto Renderer::record_frame(const Scene& scene, uint32_t image_index) -> void {
    auto& frame = frames_in_flight_.current();
    frame.reset();
    bind_swapchain_images(image_index);

    for (auto& render_pass : render_passes_) {
        render_pass->prepare(scene);
    }

    auto& command_buffer = frame.primary_command_buffer;
    command_buffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    render_graph_.record(command_buffer);
    command_buffer.end();
}

auto Renderer::submit(uint32_t image_index) -> void {
    const auto& device = device_context_.device();
    auto& frame = frames_in_flight_.current();

    const auto wait_semaphore = *frame.image_available;
    const vk::PipelineStageFlags wait_stage =
        vk::PipelineStageFlagBits::eAllCommands;
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
