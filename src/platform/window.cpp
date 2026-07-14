#include "platform/window.hpp"
#include <stdexcept>

auto Window::create(uint16_t width, uint16_t height) -> void {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    this->handle_ = glfwCreateWindow(
        width,
        height,
        "th1nk2r_renderer", 
        nullptr, 
        nullptr
    );

    if (!this->handle_) {
        glfwTerminate();
        throw std::runtime_error("failed to create the window!");
    }

    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, framebuffer_size_callback);
}

auto Window::framebuffer_size_callback(
    GLFWwindow* window,
    int,
    int
) -> void {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr) {
        self->framebuffer_resized_ = true;
    }
}

Window::~Window() {
    if (this->handle_ != nullptr) {
        glfwDestroyWindow(this->handle_);
    }
    glfwTerminate();
}
