#include "gfx/frame/frame_context.hpp"

auto FrameContext::create(const DeviceContext& device_context, const SwapchainContext& swapchain_context) -> void {
    images_count = swapchain_context.swapchain().swapchain_image_views().size();
    create_sync_objects(device_context.device());
    create_command_contexts(device_context.device());
}

auto FrameContext::recreate_swapchain_resources(
    const Device& device,
    const SwapchainContext& swapchain_context
) -> void {
    images_count = static_cast<uint32_t>(
        swapchain_context.swapchain().swapchain_image_views().size()
    );
    create_render_finished_semaphores(device);
}

auto FrameContext::create_sync_objects(const Device& device) -> void {
    image_available_.clear();
    in_flight_fence_.clear();

    image_available_.reserve(max_frames_in_flight_);
    in_flight_fence_.reserve(max_frames_in_flight_);

    const vk::SemaphoreCreateInfo semaphore_create_info{};
    const vk::FenceCreateInfo fence_create_info {
        .flags = vk::FenceCreateFlagBits::eSignaled
    };

    create_render_finished_semaphores(device);

    for (uint32_t i = 0; i < max_frames_in_flight_; ++i) {
        image_available_.push_back(
            device.logical_device().createSemaphore(semaphore_create_info)
        );
        in_flight_fence_.push_back(
            device.logical_device().createFence(fence_create_info)
        );
    }
}

auto FrameContext::create_render_finished_semaphores(const Device& device) -> void {
    render_finished_.clear();
    render_finished_.reserve(images_count);

    const vk::SemaphoreCreateInfo semaphore_create_info{};
    for (uint32_t i = 0; i < images_count; ++i) {
        render_finished_.push_back(
            device.logical_device().createSemaphore(semaphore_create_info)
        );
    }
}

auto FrameContext::create_command_contexts(const Device& device) -> void {
    command_contexts_.clear();
    command_contexts_.reserve(max_frames_in_flight_);
    for (int i = 0; i < max_frames_in_flight_; i++) {
        command_contexts_.push_back({device, device.graphics_family(), 1});
    }
}

auto FrameContext::wait(const Device& device) -> void {
    static_cast<void>(device.logical_device().waitForFences(
        *in_flight_fence_[current_frame_index_],
        true,
        std::numeric_limits<uint64_t>::max()
    ));
}

auto FrameContext::reset(const Device& device) -> void {
    device.logical_device().resetFences(
        *in_flight_fence_[current_frame_index_]
    );
}
