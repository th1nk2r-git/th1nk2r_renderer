#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/window.hpp"
#include "render/renderer.hpp"

class Application {
public:
    Application() = default;
    
    // composition root
    auto init() -> void {
        window_.create(1200, 800);
        renderer_.create(window_);
    }

    // entry point of the application
    auto run() -> void {
    while (!window_.should_close()) {
        window_.poll_events();
        // to be continued......
    }
}

private:
    Window window_;
    Renderer renderer_;
};

#endif