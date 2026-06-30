#include "Application.hpp"

auto Application::init() -> void {
    window_manager.create_window(1200, 800);
    vulkan_context.create_instance();
}

auto Application::run() -> void {
    while (!glfwWindowShouldClose(window_manager.get_window())) {
        glfwPollEvents();
    }
}