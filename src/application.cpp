#include "Application.hpp"

auto Application::init() -> void {
    window_manager.create_window(1200, 800);
    vulkan_context.create_instance();
    window_manager.create_surface(vulkan_context.get_instance());
    vulkan_context.pick_physical_device(window_manager.get_surface());
}

auto Application::run() -> void {
    while (!glfwWindowShouldClose(window_manager.get_window())) {
        glfwPollEvents();
    }
}