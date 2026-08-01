#include "core/application.hpp"

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
