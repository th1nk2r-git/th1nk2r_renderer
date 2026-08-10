#include "render/renderer.hpp"
#include "render/pipeline/basic_rendering.hpp"
#include "resource/manager/resource_registry.hpp"

#include <array>
#include <stdexcept>
#include <utility>

Renderer::Renderer(const Window& window) : 
    device_context_(window),
    swapchain_context_(device_context_, window),
    frame_context_(device_context_.device()),
    main_pipeline_(
        BasicRenderingPipelineFactory::create(
        device_context_.device(),
        swapchain_context_.render_pass())
    ),
    camera_uniforms_(
        device_context_.device(),
        device_context_.allocator(),
        main_pipeline_.descriptor_set_layout(0),
        frame_context_) {}

auto Renderer::attach_registry(const ResourceRegistry& registry) -> void {
    if (registry_ != nullptr) {
        throw std::logic_error(
            "renderer resource registry is already attached"
        );
    }
    registry_ = &registry;
}

auto Renderer::render(const Scene& scene) -> void {
    if (registry_ == nullptr) {
        throw std::logic_error(
            "renderer requires an attached resource registry"
        );
    }

    auto& device = device_context_.device();
    frame_context_.wait(device);

    const auto extent = swapchain_context_.swapchain().swapchain_image_extent();
    const auto aspect_ratio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    camera_uniforms_.update(
        ViewProjection{
            .view = scene.camera().view_matrix(),
            .projection = scene.camera().projection_matrix(aspect_ratio)
        }
    );
    auto& image_available = frame_context_.current_image_available();
    uint32_t image_id = 0;
    bool swapchain_suboptimal = false;

    const auto acquire_result = swapchain_context_.acquire(image_available);
    image_id = acquire_result.value;
    swapchain_suboptimal = acquire_result.result == vk::Result::eSuboptimalKHR;

    record(image_id, scene);
    submit(image_id);
    const auto present_result = present(image_id);

    frame_context_.advance();

    if (swapchain_suboptimal || present_result == vk::Result::eSuboptimalKHR || present_result == vk::Result::eErrorOutOfDateKHR) {
        throw vk::OutOfDateKHRError("swapchain recreation required!");
    }
}

auto Renderer::record(uint32_t image_id, const Scene& scene) -> void {
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
            vk::SubpassContents::eInline
        );
        const auto extent = swapchain_context_.swapchain().swapchain_image_extent();
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

        const std::array camera_descriptor_sets{
            *camera_uniforms_.current_descriptor_set()
        };
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *main_pipeline_.layout().get(),
            0,
            camera_descriptor_sets,
            {}
        );

        for (const auto& entity : scene.entities()) {
            const auto model_matrix = entity.model_matrix();
            cmd.pushConstants<glm::mat4>(
                *main_pipeline_.layout().get(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                model_matrix
            );

            for (const auto& mesh : entity.model().meshes()) {
                const auto& material = registry_->query(mesh.material());
                const std::array material_descriptor_sets{
                    *material.descriptor_set()
                };
                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    *main_pipeline_.layout().get(),
                    1,
                    material_descriptor_sets,
                    {}
                );

                mesh.bind(cmd);
                cmd.drawIndexed(mesh.index_count(), 1, 0, 0, 0);
            }
        }

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
        frame_context_.current_in_flight_fence()
    );
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

    const auto old_swapchain = *swapchain_context_.swapchain().get();

    auto replacement = SwapchainContext(
        device_context_,
        window,
        old_swapchain
    );
    const auto pipeline_is_compatible =
        swapchain_context_.render_pass().compatible_with(
            replacement.render_pass()
        );

    Pipeline replacement_pipeline;
    if (!pipeline_is_compatible) {
        replacement_pipeline =
            BasicRenderingPipelineFactory::create_graphics_pipeline(
                device,
                replacement.render_pass(),
                main_pipeline_.layout()
            );
    }

    swapchain_context_ = std::move(replacement);
    if (!pipeline_is_compatible) {
        main_pipeline_.replace_pipeline(
            std::move(replacement_pipeline)
        );
    }
}
