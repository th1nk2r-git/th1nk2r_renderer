#include "core/application.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {
    auto make_model_name(
        const std::filesystem::path& root,
        const std::filesystem::path& model_path
    ) -> std::string {
        const auto relative_directory = model_path.parent_path().lexically_relative(root);
        if (relative_directory.empty() || relative_directory == ".") {
            return model_path.stem().string();
        }
        return relative_directory.generic_string();
    }
}

Application::Application()
    : window_(1200, 800),
      renderer_(window_),
      resources_manager_(
        ImportContext{
            .device = renderer_.device(),
            .allocator = renderer_.allocator(),
            .uploader = renderer_.uploader(),
            .material_descriptor_set_layout = renderer_.material_descriptor_set_layout()
        }
    ) {
    renderer_.attach_registry(resources_manager_.registry());
    load_models("./assets");
}

auto Application::run() -> void {
    setup_scene();
    loop();
}

auto Application::load_models(const std::filesystem::path& root) -> void {
    std::vector<std::filesystem::path> model_paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().string();
        std::ranges::transform(
            extension,
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        if (extension == ".obj" ||
            extension == ".fbx" ||
            extension == ".gltf" ||
            extension == ".glb") {
            model_paths.push_back(entry.path());
        }
    }
    std::ranges::sort(model_paths);
    for (const auto& path : model_paths) {
        resources_manager_.import_model(path, make_model_name(root, path));
    }
}

auto Application::setup_scene() -> void {
    const auto x_rotation = glm::angleAxis(
        glm::radians(-90.0F),
        glm::vec3{1.0F, 0.0F, 0.0F}
    );

    const auto y_rotation = glm::angleAxis(
        glm::radians(-90.0F),
        glm::vec3{0.0F, 1.0F, 0.0F}
    );
    scene_.create_entity(
        resources_manager_.query_model("viking_room"),
        Transform {
            .position = {1.0F, 0.0F, -3.0F},
            .rotation = y_rotation * x_rotation
        }
    );

    scene_.create_entity(
        resources_manager_.query_model("viking_room"),
        Transform {
            .position = {-1.0F, 0.0F, -3.0F},
            .rotation = y_rotation * x_rotation
        }
    );
}

auto Application::loop() -> void {
    while (!window_.should_close()) {
        window_.poll_events();
        if (window_.consume_framebuffer_resized()) {
            renderer_.recreate_swapchain(window_);
            continue;
        }
        try {
            renderer_.render(scene_);
        }
        catch (const vk::OutOfDateKHRError&) {
            renderer_.recreate_swapchain(window_);
        }
    }
    renderer_.wait_idle();
}
