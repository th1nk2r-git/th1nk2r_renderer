#include "window_manager.hpp"
#include <stdexcept>

auto WindowManager::create_window(uint16_t width, uint16_t height) -> void {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto raw_window = glfwCreateWindow(
        width,
        height,
        "th1nk2r_renderer", 
        nullptr, 
        nullptr
    );

    if (!raw_window) {
        glfwTerminate();
        throw std::runtime_error("failed to create the window");
    }

    window.reset(raw_window);
}

WindowManager::~WindowManager() {
    window.reset();
    glfwTerminate();
}