#ifndef WINDOW_HPP
#define WINDOW_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>
#include <functional>
#include <utility>

class Window {
public:
    using KeyCallback = std::function<void(int key, int action)>;
    using CursorPositionCallback =
        std::function<void(double x, double y)>;

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

    auto set_key_callback(KeyCallback callback) -> void {
        key_callback_ = std::move(callback);
    }

    auto set_cursor_position_callback(
        CursorPositionCallback callback
    ) -> void {
        cursor_position_callback_ = std::move(callback);
    }

private:
    GLFWwindow* handle_ = nullptr;
    bool framebuffer_resized_ = false;
    KeyCallback key_callback_;
    CursorPositionCallback cursor_position_callback_;

    static auto framebuffer_size_callback(
        GLFWwindow* window,
        int width,
        int height
    ) -> void;
    static auto key_callback(
        GLFWwindow* window,
        int key,
        int scancode,
        int action,
        int mods
    ) -> void;
    static auto cursor_position_callback(
        GLFWwindow* window,
        double x,
        double y
    ) -> void;
};

#endif
