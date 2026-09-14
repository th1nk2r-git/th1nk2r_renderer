#include "render/renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <stdexcept>
#include <utility>

#include "core/thread_pool.hpp"
#include "gfx/device/device_context.hpp"
#include "gfx/frame/frames_in_flight.hpp"
#include "gfx/frame/swapchain.hpp"
#include "platform/window.hpp"
#include "render/pass/forward/forward_pass.hpp"
#include "render/pass/shadow/shadow_pass.hpp"

namespace {
    constexpr std::size_t shadow_recording_slot = 0;
    constexpr std::size_t forward_recording_slot = 1;
}

struct Renderer::Impl {
    DeviceContext& device_context;
    Window& window;
    const ResourceRegistry& registry;
    ThreadPool<8>& thread_pool;
    Swapchain swapchain;
    FramesInFlight frames_in_flight;
    ShadowPass shadow_pass;
    ForwardPass forward_pass;

    Impl(
        DeviceContext& context,
        Window& target_window,
        const ResourceRegistry& resources,
        ThreadPool<8>& pool
    )
        : device_context(context),
          window(target_window),
          registry(resources),
          thread_pool(pool),
          swapchain(context, target_window),
          frames_in_flight(context.device()),
          shadow_pass(
              context.device(),
              context.allocator(),
              frames_in_flight.frame_count()
          ),
          forward_pass(
              context.device(),
              context.allocator(),
              swapchain.render_pass(),
              shadow_pass.descriptor_set_layout(),
              frames_in_flight.frame_count()
          ) {}

    ~Impl() noexcept {
        try {
            wait_idle();
        }
        catch (...) {
            // Normal shutdown reports errors through wait_idle(); unwinding cannot.
        }
    }

    auto wait_idle() const -> void {
        device_context.device().logical_device().waitIdle();
    }

    auto recreate_swapchain() -> void {
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

        wait_idle();
        auto replacement = Swapchain(device_context, window, *swapchain.handle());
        forward_pass.recreate_pipeline(
            device_context.device(),
            replacement.render_pass()
        );
        swapchain = std::move(replacement);

        // A restore event may have arrived while waiting for a nonzero extent.
        window.consume_framebuffer_resized();
    }

    auto record_scene(uint32_t image_id, const Scene& scene) -> void {
        auto& frame = frames_in_flight.current_frame();
        const auto frame_index = frames_in_flight.current_frame_index();
        const auto shadow_output = shadow_pass.prepare(frame_index, scene);
        const ForwardPass::Output target{
            .render_pass = swapchain.render_pass(),
            .framebuffer = swapchain.framebuffers().at(image_id),
            .extent = swapchain.extent()
        };

        auto shadow_future = thread_pool.run(
            [this, &frame, &scene, &shadow_output, frame_index] {
                frame.record(shadow_recording_slot, [&, this](
                    vk::raii::CommandBuffer& command_buffer
                ) {
                    shadow_pass.record(
                        ShadowPass::ExecutionContext{
                            .command_buffer = command_buffer,
                            .frame_index = frame_index
                        },
                        ShadowPass::Input{.scene = scene, .registry = registry},
                        shadow_output
                    );
                });
            }
        );

        std::future<void> forward_future;
        try {
            forward_future = thread_pool.run(
                [this, &frame, &scene, &shadow_output, &target, frame_index] {
                    frame.record(forward_recording_slot, [&, this](
                        vk::raii::CommandBuffer& command_buffer
                    ) {
                        forward_pass.record(
                            ForwardPass::ExecutionContext{
                                .command_buffer = command_buffer,
                                .frame_index = frame_index
                            },
                            ForwardPass::Input{
                                .scene = scene,
                                .registry = registry,
                                .shadow = shadow_output
                            },
                            target
                        );
                    });
                }
            );
        }
        catch (...) {
            shadow_future.wait();
            throw;
        }

        shadow_future.wait();
        forward_future.wait();
        shadow_future.get();
        forward_future.get();
    }

    auto submit(uint32_t image_id) -> void {
        const auto& device = device_context.device();
        auto& frame = frames_in_flight.current_frame();
        const auto wait_semaphore = *frame.image_available;
        const auto wait_stage =
            vk::PipelineStageFlagBits::eColorAttachmentOutput |
            vk::PipelineStageFlagBits::eEarlyFragmentTests |
            vk::PipelineStageFlagBits::eLateFragmentTests;
        const auto command_buffers = frame.command_buffers();
        const auto signal_semaphore = *swapchain.render_finished(image_id);

        vk::SubmitInfo submit_info{};
        submit_info
            .setWaitSemaphores(wait_semaphore)
            .setWaitDstStageMask(wait_stage)
            .setCommandBuffers(command_buffers)
            .setSignalSemaphores(signal_semaphore);

        frames_in_flight.reset(device);
        device.graphics_queue().submit(submit_info, frame.in_flight_fence);
    }

    auto present(uint32_t image_id) -> vk::Result {
        const auto wait_semaphore = *swapchain.render_finished(image_id);
        const auto handle = *swapchain.handle();

        vk::PresentInfoKHR present_info{};
        present_info
            .setWaitSemaphores(wait_semaphore)
            .setSwapchains(handle)
            .setImageIndices(image_id);

        try {
            const auto result = device_context.device().present_queue().presentKHR(present_info);
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

    auto render(const Scene& scene) -> FrameResult {
        if (window.should_close()) {
            return FrameResult::Skipped;
        }
        if (window.consume_framebuffer_resized()) {
            recreate_swapchain();
            return FrameResult::Skipped;
        }

        frames_in_flight.wait(device_context.device());

        std::optional<vk::ResultValue<uint32_t>> acquired;
        try {
            acquired = swapchain.acquire(frames_in_flight.current_frame().image_available);
        }
        catch (const vk::OutOfDateKHRError&) {
            recreate_swapchain();
            return FrameResult::Skipped;
        }

        const auto image_id = acquired->value;
        record_scene(image_id, scene);
        submit(image_id);
        const auto present_result = present(image_id);
        frames_in_flight.advance();

        if (acquired->result == vk::Result::eSuboptimalKHR ||
            present_result == vk::Result::eSuboptimalKHR ||
            present_result == vk::Result::eErrorOutOfDateKHR) {
            recreate_swapchain();
            return FrameResult::Skipped;
        }
        return FrameResult::Rendered;
    }
};

Renderer::Renderer(
    DeviceContext& device_context,
    Window& window,
    const ResourceRegistry& registry,
    ThreadPool<8>& thread_pool
) : impl_(std::make_unique<Impl>(device_context, window, registry, thread_pool)) {}

Renderer::~Renderer() noexcept = default;

auto Renderer::prepare_resources(std::span<const ResourceId<Material>> material_ids) -> void {
    impl_->device_context.buffer_uploader().submit_and_wait();
    impl_->device_context.image_uploader().submit_and_wait();
    impl_->shadow_pass.write_material(material_ids, impl_->registry);
    impl_->forward_pass.write_material(material_ids, impl_->registry);
}

auto Renderer::set_environment(const HdrImageData& panorama) -> void {
    impl_->forward_pass.write_environment(panorama, impl_->device_context.image_uploader());
}

auto Renderer::render(const Scene& scene) -> FrameResult {
    return impl_->render(scene);
}

auto Renderer::wait_idle() const -> void {
    impl_->wait_idle();
}
