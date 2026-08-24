#include "ui/imgui_layer.hpp"

#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "gfx/device/device_context.hpp"
#include "platform/window.hpp"

namespace {
    constexpr uint32_t minimum_image_count = 2;
    constexpr uint32_t descriptor_pool_size = 64;
}

ImGuiLayer::ImGuiLayer(
    const Window& window,
    const DeviceContext& device_context,
    const vk::raii::RenderPass& render_pass,
    uint32_t image_count
) : device_context_(device_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    context_created_ = true;

    try {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window.get(), false)) {
            throw std::runtime_error(
                "failed to initialize ImGui GLFW backend!"
            );
        }
        glfw_initialized_ = true;
        initialize_vulkan(render_pass, image_count);
    }
    catch (...) {
        shutdown();
        throw;
    }
}

ImGuiLayer::~ImGuiLayer() noexcept {
    shutdown();
}

auto ImGuiLayer::prepare_frame(
    bool fps_enabled,
    double average_fps
) const -> void {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (fps_enabled) {
        ImGui::SetNextWindowPos(
            ImVec2{10.0F, 10.0F},
            ImGuiCond_Always
        );
        ImGui::SetNextWindowBgAlpha(0.0F);

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            0.0F
        );

        constexpr auto flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        ImGui::Begin("##fps_overlay", nullptr, flags);
        ImGui::PushFont(nullptr, 40.0F);
        ImGui::Text("FPS: %.1f", average_fps);
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
    }
    ImGui::Render();
}

auto ImGuiLayer::record(
    vk::raii::CommandBuffer& command_buffer
) const -> void {
    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(),
        static_cast<VkCommandBuffer>(*command_buffer)
    );
}

auto ImGuiLayer::recreate(
    const vk::raii::RenderPass& render_pass,
    uint32_t image_count
) -> void {
    shutdown_vulkan();
    initialize_vulkan(render_pass, image_count);
}

auto ImGuiLayer::initialize_vulkan(
    const vk::raii::RenderPass& render_pass,
    uint32_t image_count
) -> void {
    if (image_count < minimum_image_count) {
        throw std::runtime_error(
            "ImGui Vulkan backend requires at least two swapchain images!"
        );
    }

    const auto& device = device_context_.device();
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_4;
    init_info.Instance = static_cast<VkInstance>(*device_context_.instance());
    init_info.PhysicalDevice =
        static_cast<VkPhysicalDevice>(*device.physical_device());
    init_info.Device = static_cast<VkDevice>(*device.logical_device());
    init_info.QueueFamily = device.graphics_family();
    init_info.Queue = static_cast<VkQueue>(*device.graphics_queue());
    init_info.DescriptorPoolSize = descriptor_pool_size;
    init_info.MinImageCount = minimum_image_count;
    init_info.ImageCount = image_count;
    init_info.PipelineInfoMain.RenderPass =
        static_cast<VkRenderPass>(*render_pass);
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error(
            "failed to initialize ImGui Vulkan backend!"
        );
    }
    vulkan_initialized_ = true;
}

auto ImGuiLayer::shutdown_vulkan() noexcept -> void {
    if (vulkan_initialized_) {
        ImGui_ImplVulkan_Shutdown();
        vulkan_initialized_ = false;
    }
}

auto ImGuiLayer::shutdown() noexcept -> void {
    shutdown_vulkan();
    if (glfw_initialized_) {
        ImGui_ImplGlfw_Shutdown();
        glfw_initialized_ = false;
    }
    if (context_created_) {
        ImGui::DestroyContext();
        context_created_ = false;
    }
}
