#include "core/application.hpp"

#include "io/image_loader.hpp"
#include "resource/importer/model_importer.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"

Application::Application()
    : window_(1200, 800),
      device_context_(window_),
      renderer_(device_context_, window_, registry_),
      input_system_(window_, scene_.camera()) {}

auto Application::run() -> void {
    ModelImporter importer{device_context_, registry_};
    const auto imported = importer.import_models("./assets/models");
    renderer_.prepare_resources(imported.materials);
    renderer_.set_environment(
        load_image_rgba32f("./assets/models/sponza/mud_road_puresky_2k.hdr")
    );
    setup_scene();
    loop();
}

auto Application::setup_scene() -> void {
    const auto sponza_model = registry_.query_model_id("sponza");
    auto& sponza = scene_.create_entity();
    sponza.add_component<Transform>();
    sponza.add_component<MeshRenderer>(sponza_model);

    scene_.camera().set_position(glm::vec3{-2.0F, 2.0F, 7.0F});

    scene_.add_point_light(
        PointLight{
            .position = glm::vec3{0.0F, 6.0F, 0.0F},
            .color = glm::vec3{1.0F, 0.9F, 0.75F},
            .intensity = 100.0F,
            .casts_shadow = true,
            .shadow_near = 0.1F,
            .shadow_far = 25.0F,
            .source_radius = 0.2F
        }
    );
}

auto Application::update(float delta_time) -> void {
    input_system_.update(delta_time);
}

auto Application::loop() -> void {
    timer_.reset();

    while (!window_.should_close()) {
        window_.poll_events();
        auto delta_time = timer_.tick();
        update(delta_time);
        auto render_result = renderer_.render(scene_);
        if (render_result == Renderer::FrameResult::Skipped) {
            timer_.reset();
        }
    }
    renderer_.wait_idle();
}
