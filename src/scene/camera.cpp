#include "scene/camera.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

Camera::Camera(
    glm::vec3 position,
    glm::quat orientation,
    float vertical_fov,
    float near_plane,
    float far_plane
) : position_(position) {
    set_orientation(orientation);
    set_perspective(vertical_fov, near_plane, far_plane);
}

auto Camera::set_orientation(const glm::quat& orientation) -> void {
    const auto length_squared = glm::dot(orientation, orientation);
    if (!std::isfinite(length_squared) ||
        length_squared <= std::numeric_limits<float>::epsilon()) {
        throw std::invalid_argument(
            "camera orientation must be a finite, non-zero quaternion"
        );
    }

    orientation_ = glm::normalize(orientation);
}

auto Camera::set_perspective(
    float vertical_fov,
    float near_plane,
    float far_plane
) -> void {
    if (!std::isfinite(vertical_fov) ||
        vertical_fov <= 0.0F ||
        vertical_fov >= glm::pi<float>()) {
        throw std::invalid_argument(
            "camera vertical field of view must be between 0 and pi radians"
        );
    }
    if (!std::isfinite(near_plane) || near_plane <= 0.0F) {
        throw std::invalid_argument(
            "camera near plane must be finite and greater than zero"
        );
    }
    if (!std::isfinite(far_plane) || far_plane <= near_plane) {
        throw std::invalid_argument(
            "camera far plane must be finite and greater than the near plane"
        );
    }

    vertical_fov_ = vertical_fov;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
}

auto Camera::view_matrix() const -> glm::mat4 {
    const auto inverse_rotation = glm::mat4_cast(
        glm::conjugate(orientation_)
    );
    const auto inverse_translation = glm::translate(
        glm::mat4{1.0F},
        -position_
    );
    return inverse_rotation * inverse_translation;
}

auto Camera::projection_matrix(float aspect_ratio) const -> glm::mat4 {
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
        throw std::invalid_argument(
            "camera aspect ratio must be finite and greater than zero"
        );
    }

    auto projection = glm::perspectiveRH_ZO(
        vertical_fov_,
        aspect_ratio,
        near_plane_,
        far_plane_
    );
    projection[1][1] *= -1.0F;
    return projection;
}

auto Camera::forward() const -> glm::vec3 {
    return orientation_ * glm::vec3{0.0F, 0.0F, -1.0F};
}

auto Camera::right() const -> glm::vec3 {
    return orientation_ * glm::vec3{1.0F, 0.0F, 0.0F};
}

auto Camera::up() const -> glm::vec3 {
    return orientation_ * glm::vec3{0.0F, 1.0F, 0.0F};
}
