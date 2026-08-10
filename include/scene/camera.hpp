#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

class Camera {
public:
    Camera() = default;

    Camera(
        glm::vec3 position,
        glm::quat orientation,
        float vertical_fov,
        float near_plane,
        float far_plane
    );

    auto position() const noexcept -> const glm::vec3& {
        return position_;
    }

    auto orientation() const noexcept -> const glm::quat& {
        return orientation_;
    }

    auto set_position(const glm::vec3& position) noexcept -> void {
        position_ = position;
    }

    auto set_orientation(const glm::quat& orientation) -> void;

    auto set_perspective(
        float vertical_fov,
        float near_plane,
        float far_plane
    ) -> void;

    auto view_matrix() const -> glm::mat4;
    auto projection_matrix(float aspect_ratio) const -> glm::mat4;

    auto forward() const -> glm::vec3;
    auto right() const -> glm::vec3;
    auto up() const -> glm::vec3;

private:
    glm::vec3 position_{0.0F};
    glm::quat orientation_{
        1.0F, 0.0F, 0.0F, 0.0F
    };

    float vertical_fov_{
        glm::radians(45.0F)
    };
    float near_plane_{0.1F};
    float far_plane_{100.0F};
};

#endif
