#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "core/input/input_system.hpp"
#include "core/thread_pool.hpp"
#include "core/timer.hpp"
#include "gfx/device/device_context.hpp"
#include "platform/window.hpp"
#include "render/renderer.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/scene.hpp"
#include "scene/terrain_generator.hpp"

class Application {
public:
    Application();

    Application(const Application&) = delete;
    auto operator=(const Application&) -> Application& = delete;
    Application(Application&&) = delete;
    auto operator=(Application&&) -> Application& = delete;

    // entry point of the application
    auto run() -> void;

private:
    Scene scene_;

    Window window_;
    DeviceContext device_context_;
    AssetsDB assets_;
    InputSystem input_system_;
    Timer timer_;
    ThreadPool thread_pool_{8};
    Renderer renderer_;

    // setup the main scene
    auto setup_scene() -> void;

    // update the application state
    auto update(float delta_time) -> void;

    // the main loop of the application
    auto loop() -> void;
};

#endif
