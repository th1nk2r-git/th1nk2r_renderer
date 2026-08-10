#ifndef ENTITY_HPP
#define ENTITY_HPP

#include "resource/model.hpp"
#include "utils/data/transform.hpp"

#include <glm/mat4x4.hpp>

class Entity {
public:
    Entity() = delete;
    Entity(const Model& model, Transform transform) noexcept;

    Entity(const Entity&) = delete;
    auto operator=(const Entity&) -> Entity& = delete;
    Entity(Entity&&) noexcept = default;
    auto operator=(Entity&&) noexcept -> Entity& = delete;

    auto model() const noexcept -> const Model& {
        return model_;
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
    const Model& model_;
    Transform transform_;
};

#endif
