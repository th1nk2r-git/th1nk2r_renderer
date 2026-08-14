#include "scene/entity.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <stdexcept>
#include <utility>

Entity::Entity(ResourceId<Model> model, Transform transform)
    : model_id_(model), transform_(std::move(transform)) {
    if (!model_id_.valid()) {
        throw std::invalid_argument("entity model resource id is invalid");
    }
}

auto Entity::set_transform(Transform transform) noexcept -> void {
    transform_ = std::move(transform);
}

auto Entity::model_matrix() const noexcept -> glm::mat4 {
    auto matrix = glm::translate(
        glm::mat4{1.0F},
        transform_.position
    );
    matrix *= glm::mat4_cast(transform_.rotation);
    return glm::scale(matrix, transform_.scale);
}
