#include "scene/scene.hpp"

#include <utility>

auto Scene::set_camera(Camera&& camera) noexcept -> void {
    camera_ = std::move(camera);
}

auto Scene::create_entity(
    const Model& model,
    Transform transform
) -> Entity& {
    entities_.emplace_back(model, std::move(transform));
    return entities_.back();
}

auto Scene::clear_entities() noexcept -> void {
    entities_.clear();
}
