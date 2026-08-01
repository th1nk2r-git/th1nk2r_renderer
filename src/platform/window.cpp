#include "platform/window.hpp"
#include <stdexcept>
#include <utility>

Window::Window(uint16_t width, uint16_t height) {
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

Window::Window(Window&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      framebuffer_resized_(std::exchange(other.framebuffer_resized_, false)) {
    if (handle_ != nullptr) {
        glfwSetWindowUserPointer(handle_, this);
    }
}

auto Window::operator=(Window&& other) noexcept -> Window& {
    if (this == &other) {
        return *this;
    }

    const bool released_last_window = handle_ != nullptr && other.handle_ == nullptr;
    if (handle_ != nullptr) {
        glfwDestroyWindow(handle_);
    }

    handle_ = std::exchange(other.handle_, nullptr);
    framebuffer_resized_ = std::exchange(other.framebuffer_resized_, false);

    if (handle_ != nullptr) {
        glfwSetWindowUserPointer(handle_, this);
    }
    else if (released_last_window) {
        glfwTerminate();
    }

    return *this;
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

Window::~Window() noexcept {
    if (handle_ != nullptr) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        glfwTerminate();
    }
}
