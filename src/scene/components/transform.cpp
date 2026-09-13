#include "scene/components/transform.hpp"

#include <utility>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

Transform::Transform(
    glm::vec3 position,
    glm::quat rotation,
    glm::vec3 scale
) noexcept
    : position_(std::move(position)),
      rotation_(std::move(rotation)),
      scale_(std::move(scale)) {}

auto Transform::model_matrix() const noexcept -> glm::mat4 {
    auto matrix = glm::translate(
        glm::mat4{1.0F},
        position_
    );
    matrix *= glm::mat4_cast(rotation_);
    return glm::scale(matrix, scale_);
}
