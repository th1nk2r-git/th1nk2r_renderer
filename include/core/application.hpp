#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include <filesystem>

#include "platform/window.hpp"
#include "render/renderer.hpp"
#include "resource/manager/resource_manager.hpp"
#include "scene/scene.hpp"

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
    Window window_;
    Renderer renderer_;
    ResourceManager resources_manager_;
    Scene scene_;

    // load the models
    auto load_models(const std::filesystem::path& root) -> void;

    // setup the main scene
    auto setup_scene() -> void;

    // the main loop of the application
    auto loop() -> void;
};

#endif
