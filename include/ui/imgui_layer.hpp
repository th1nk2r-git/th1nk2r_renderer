#ifndef IMGUI_LAYER_HPP
#define IMGUI_LAYER_HPP

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

class DeviceContext;
class Window;

class ImGuiLayer {
public:
    ImGuiLayer(
        const Window& window,
        const DeviceContext& device_context,
        const vk::raii::RenderPass& render_pass,
        uint32_t image_count
    );
    ~ImGuiLayer() noexcept;

    ImGuiLayer(const ImGuiLayer&) = delete;
    auto operator=(const ImGuiLayer&) -> ImGuiLayer& = delete;
    ImGuiLayer(ImGuiLayer&&) = delete;
    auto operator=(ImGuiLayer&&) -> ImGuiLayer& = delete;

    // build the non-interactive FPS overlay for the current frame
    auto prepare_frame(
        bool fps_enabled,
        double average_fps
    ) const -> void;

    // record ImGui draw data inside the active main render pass
    auto record(vk::raii::CommandBuffer& command_buffer) const -> void;

    // rebuild the Vulkan backend for a replacement swapchain/render pass
    auto recreate(
        const vk::raii::RenderPass& render_pass,
        uint32_t image_count
    ) -> void;

private:
    const DeviceContext& device_context_;
    bool context_created_ = false;
    bool glfw_initialized_ = false;
    bool vulkan_initialized_ = false;

    auto initialize_vulkan(
        const vk::raii::RenderPass& render_pass,
        uint32_t image_count
    ) -> void;
    auto shutdown_vulkan() noexcept -> void;
    auto shutdown() noexcept -> void;
};

#endif
