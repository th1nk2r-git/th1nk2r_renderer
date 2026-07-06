#ifndef WINDOW_HPP
#define WINDOW_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <memory>

class Window {
public:
    Window() = default;

    ~Window();

    // create the window
    auto create(uint16_t width, uint16_t height) -> void;

    // return the flag for whether the window got closed 
    auto should_close() const -> bool {
        return glfwWindowShouldClose(handle_);
    }

    // Handle the events
    auto poll_events() const -> void {
        glfwPollEvents();
    }

    // return the raw pointer of the window
    auto get() const -> GLFWwindow* {
        return handle_;
    }

private:
    GLFWwindow* handle_ = nullptr;
};

#endif