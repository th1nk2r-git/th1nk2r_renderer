#include "application.hpp"

auto Application::init() -> void {
    window.create(1200, 800);
    vulkan_context.create_instance();
    surface.create(vulkan_context.get_instance(), window);
    vulkan_context.pick_physical_device(surface.get());
    vulkan_context.create_logical_device(surface.get());
    vulkan_context.create_graphics_queue();
    vulkan_context.create_present_queue();
    swapchain.create(vulkan_context, surface);
}

auto Application::run() -> void {
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
    }
}