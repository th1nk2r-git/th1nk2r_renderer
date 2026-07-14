#include "core/application.hpp"

auto Application::init() -> void {
    window_.create(1200, 800);
    renderer_.create(window_);
}

auto Application::run() -> void {
    while (!window_.should_close()) {
        window_.poll_events();

        if (window_.consume_framebuffer_resized()) {
            renderer_.recreate_swapchain(window_);
            continue;
        }

        try {
            renderer_.render();
        }
        catch (const vk::OutOfDateKHRError&) {
            renderer_.recreate_swapchain(window_);
        }
    }

    renderer_.wait_idle();
}
