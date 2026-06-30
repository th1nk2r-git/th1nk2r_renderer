#ifndef WINDOW_MANAGER
#define WINDOW_MANAGER

#include <GLFW/glfw3.h>
#include <memory>

class WindowManager {
public:
    WindowManager() : window(nullptr, glfwDestroyWindow) {}

    // create the window
    auto create_window(uint16_t width, uint16_t height) -> void;

    auto get_window() -> GLFWwindow* {
        return window.get();
    }

    ~WindowManager();

private:
    std::unique_ptr<GLFWwindow, void(*)(GLFWwindow*)> window;

};

#endif