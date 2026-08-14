#include "core/input/controller/camera_controller.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

CameraController::CameraController(Camera& camera) noexcept
    : camera_(camera) {
    const auto forward = camera_.forward();
    yaw_ = std::atan2(-forward.x, -forward.z);
    pitch_ = std::asin(std::clamp(forward.y, -1.0F, 1.0F));
}

auto CameraController::look(double cursor_x, double cursor_y) -> void {
    if (first_cursor_sample_) {
        previous_cursor_x_ = cursor_x;
        previous_cursor_y_ = cursor_y;
        first_cursor_sample_ = false;
        return;
    }

    const auto cursor_delta_x = static_cast<float>(
        cursor_x - previous_cursor_x_
    );
    const auto cursor_delta_y = static_cast<float>(
        cursor_y - previous_cursor_y_
    );
    previous_cursor_x_ = cursor_x;
    previous_cursor_y_ = cursor_y;

    yaw_ -= cursor_delta_x * mouse_sensitivity_;
    pitch_ -= cursor_delta_y * mouse_sensitivity_;
    const auto pitch_limit = glm::radians(89.0F);
    pitch_ = std::clamp(pitch_, -pitch_limit, pitch_limit);

    const auto yaw_rotation = glm::angleAxis(
        yaw_,
        glm::vec3{0.0F, 1.0F, 0.0F}
    );
    const auto pitch_rotation = glm::angleAxis(
        pitch_,
        glm::vec3{1.0F, 0.0F, 0.0F}
    );
    camera_.set_orientation(yaw_rotation * pitch_rotation);
}

auto CameraController::update(float delta_time) -> void {
    glm::vec3 movement{0.0F};
    if (forward_active_) {
        movement += camera_.forward();
    }
    if (backward_active_) {
        movement -= camera_.forward();
    }
    if (right_active_) {
        movement += camera_.right();
    }
    if (left_active_) {
        movement -= camera_.right();
    }
    if (up_active_) {
        movement += glm::vec3{0.0F, 1.0F, 0.0F};
    }
    if (down_active_) {
        movement -= glm::vec3{0.0F, 1.0F, 0.0F};
    }

    const auto movement_length_squared = glm::dot(movement, movement);
    if (movement_length_squared == 0.0F) {
        return;
    }

    auto speed = movement_speed_;
    if (fast_active_) {
        speed *= fast_multiplier_;
    }
    camera_.set_position(
        camera_.position() +
        glm::normalize(movement) * speed * delta_time
    );
}
