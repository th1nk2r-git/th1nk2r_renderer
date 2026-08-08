#ifndef WINDOW_HPP
#define WINDOW_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>

class Window {
public:
    Window(uint16_t width, uint16_t height);

    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    Window(Window&&) = delete;
    auto operator=(Window&&) -> Window& = delete;

    ~Window() noexcept;

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

    // return and clear the framebuffer resize flag.
    auto consume_framebuffer_resized() -> bool {
        const bool resized = framebuffer_resized_;
        framebuffer_resized_ = false;
        return resized;
    }

private:
    GLFWwindow* handle_ = nullptr;
    bool framebuffer_resized_ = false;

    static auto framebuffer_size_callback(GLFWwindow* window, int width, int height) -> void;
};

#endif
