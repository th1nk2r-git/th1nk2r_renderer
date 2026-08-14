#ifndef ENTITY_HPP
#define ENTITY_HPP

#include "resource/gpu/resource_id.hpp"
#include "scene/transform.hpp"

#include <glm/mat4x4.hpp>

class Model;

class Entity {
public:
    Entity() = delete;
    Entity(ResourceId<Model> model, Transform transform);

    Entity(const Entity&) = delete;
    auto operator=(const Entity&) -> Entity& = delete;
    Entity(Entity&&) noexcept = default;
    auto operator=(Entity&&) noexcept -> Entity& = delete;

    auto model_id() const noexcept -> ResourceId<Model> {
        return model_id_;
    }

    auto transform() noexcept -> Transform& {
        return transform_;
    }

    auto transform() const noexcept -> const Transform& {
        return transform_;
    }

    auto set_transform(Transform transform) noexcept -> void;

    auto model_matrix() const noexcept -> glm::mat4;

private:
    ResourceId<Model> model_id_;
    Transform transform_;
};

#endif
