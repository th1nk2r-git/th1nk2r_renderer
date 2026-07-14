#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/window.hpp"
#include "render/renderer.hpp"

class Application {
public:
    Application() = default;
    
    // composition root
    auto init() -> void;

    // entry point of the application
    auto run() -> void;

private:
    Window window_;
    Renderer renderer_;
};

#endif