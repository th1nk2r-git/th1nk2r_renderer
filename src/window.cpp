#include "window.hpp"
#include <stdexcept>

auto Window::create(uint16_t width, uint16_t height) -> void {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    handle = glfwCreateWindow(
        width,
        height,
        "th1nk2r_renderer", 
        nullptr, 
        nullptr
    );

    if (!handle) {
        glfwTerminate();
        throw std::runtime_error("failed to create the window!");
    }
}

Window::~Window() {
    glfwDestroyWindow(handle);
    glfwTerminate();
}