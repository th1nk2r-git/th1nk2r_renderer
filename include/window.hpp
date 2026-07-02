#ifndef WINDOW
#define WINDOW

#include <GLFW/glfw3.h>
#include <memory>

class Window {
public:
    Window() = default;

    // create the window
    auto create(uint16_t width, uint16_t height) -> void;

    // return the raw pointer of the window
    auto get() const -> GLFWwindow* {
        return handle;
    }

    ~Window();

private:
    GLFWwindow* handle = nullptr;
};

#endif