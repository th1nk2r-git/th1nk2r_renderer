#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include <filesystem>
#include <vector>

#include "core/frame_rate_counter.hpp"
#include "core/input/input_system.hpp"
#include "gfx/device/device_context.hpp"
#include "platform/window.hpp"
#include "render/pass/forward/forward_pass.hpp"
#include "render/pass/shadow/shadow_pass.hpp"
#include "render/renderer.hpp"
#include "resource/registry/resource_registry.hpp"
#include "scene/scene.hpp"
#include "ui/imgui_layer.hpp"

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
    Renderer renderer_;
    ShadowPass shadow_pass_;
    ForwardPass forward_pass_;
    ResourceRegistry registry_;
    bool fps_enabled_ = true;
    FrameRateCounter frame_rate_counter_;
    InputSystem input_system_;
    ImGuiLayer imgui_layer_;

    // load the models
    auto load_models(const std::filesystem::path& root) -> std::vector<ResourceId<Material>>;

    // setup the main scene
    auto setup_scene() -> void;

    // update the application state
    auto update(float delta_time) -> void;

    // the main loop of the application
    auto loop() -> void;
};

#endif
