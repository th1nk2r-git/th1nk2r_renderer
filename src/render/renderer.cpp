#include "render/renderer.hpp"

#include <stdexcept>
#include <utility>

Renderer::Renderer(
    const DeviceContext& device_context,
    const Window& window
) : swapchain_(device_context, window), frames_in_flight_(device_context.device()) {}

auto Renderer::submit(const Device& device, uint32_t image_id) -> void {
    auto& frame = frames_in_flight_.current_frame();
    const auto wait_semaphore = *frame.image_available;
    const auto wait_stage =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests |
        vk::PipelineStageFlagBits::eLateFragmentTests;
    const auto command_buffer = *frame.command_buffer;
    const auto signal_semaphore = *swapchain_.render_finished(image_id);

    vk::SubmitInfo submit_info{};
    submit_info
        .setWaitSemaphores(wait_semaphore)
        .setWaitDstStageMask(wait_stage)
        .setCommandBuffers(command_buffer)
        .setSignalSemaphores(signal_semaphore);

    frames_in_flight_.reset(device);
    device.graphics_queue().submit(
        submit_info,
        frame.in_flight_fence
    );
}

auto Renderer::present(const Device& device, uint32_t image_id) -> vk::Result {
    const auto wait_semaphore = *swapchain_.render_finished(image_id);
    const auto swapchain = *swapchain_.handle();

    vk::PresentInfoKHR present_info{};
    present_info
        .setWaitSemaphores(wait_semaphore)
        .setSwapchains(swapchain)
        .setImageIndices(image_id);

    try {
        const auto result = device.present_queue().presentKHR(present_info);
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

auto Renderer::recreate_swapchain(
    const DeviceContext& device_context,
    const Window& window
) -> bool {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window.get(), &width, &height);

    while ((width == 0 || height == 0) && !window.should_close()) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window.get(), &width, &height);
    }

    if (window.should_close()) {
        return false;
    }

    const auto& device = device_context.device();
    device.logical_device().waitIdle();

    const auto old_swapchain = *swapchain_.handle();
    auto replacement = Swapchain(
        device_context,
        window,
        old_swapchain
    );
    swapchain_ = std::move(replacement);
    return true;
}
