#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/window.hpp"
#include "render/renderer.hpp"

class Application {
public:
    Application() : window_(1200, 800), renderer_(window_) {};

    Application(const Application&) = delete;
    auto operator=(const Application&) -> Application& = delete;
    Application(Application&&) = delete;
    auto operator=(Application&&) -> Application& = delete;

    // entry point of the application
    auto run() -> void;

private:
    Window window_;
    Renderer renderer_;
};

#endif
