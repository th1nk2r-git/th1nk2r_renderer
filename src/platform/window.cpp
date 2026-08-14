#include "platform/window.hpp"
#include <stdexcept>

namespace {
    auto create_window(uint16_t width, uint16_t height) -> GLFWwindow* {
        if (!glfwInit()) {
            throw std::runtime_error("failed to initialize GLFW!");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        auto* handle = glfwCreateWindow(
            width,
            height,
            "th1nk2r_renderer",
            nullptr,
            nullptr
        );

        if (handle == nullptr) {
            glfwTerminate();
            throw std::runtime_error("failed to create the window!");
        }

        return handle;
    }
}

Window::Window(uint16_t width, uint16_t height)
    : handle_(create_window(width, height)) {
    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, framebuffer_size_callback);
    glfwSetKeyCallback(handle_, key_callback);
    glfwSetCursorPosCallback(handle_, cursor_position_callback);
}

auto Window::key_callback(
    GLFWwindow* window,
    int key,
    int,
    int action,
    int
) -> void {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->key_callback_) {
        self->key_callback_(key, action);
    }
}

auto Window::cursor_position_callback(
    GLFWwindow* window,
    double x,
    double y
) -> void {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->cursor_position_callback_) {
        self->cursor_position_callback_(x, y);
    }
}

auto Window::framebuffer_size_callback(GLFWwindow* window, int, int) -> void {
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
