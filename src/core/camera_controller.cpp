#include "core/camera_controller.hpp"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
    auto key_is_down(GLFWwindow* window, int key) -> bool {
        return glfwGetKey(window, key) == GLFW_PRESS;
    }
}

auto CameraController::set_cursor_captured(const Window& window, bool captured) -> void {
    if (cursor_captured_ == captured) {
        return;
    }

    auto* handle = window.get();
    glfwSetInputMode(
        handle,
        GLFW_CURSOR,
        captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
    );

    if (captured &&
        glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(
            handle,
            GLFW_RAW_MOUSE_MOTION,
            GLFW_TRUE
        );
    }

    cursor_captured_ = captured;
    first_cursor_sample_ = true;
}

auto CameraController::enable(const Window& window) -> void {
    set_cursor_captured(window, true);
}

auto CameraController::update(
    Camera& camera,
    const Window& window,
    float delta_time
) -> void {
    auto* handle = window.get();

    const auto alt_is_down =
        key_is_down(handle, GLFW_KEY_LEFT_ALT) ||
        key_is_down(handle, GLFW_KEY_RIGHT_ALT);

    set_cursor_captured(window, !alt_is_down);
    if (!cursor_captured_) {
        return;
    }

    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(handle, &cursor_x, &cursor_y);

    if (first_cursor_sample_) {
        previous_cursor_x_ = cursor_x;
        previous_cursor_y_ = cursor_y;
        first_cursor_sample_ = false;
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
    pitch_ = std::clamp(
        pitch_,
        -pitch_limit,
        pitch_limit
    );

    const auto yaw_rotation = glm::angleAxis(
        yaw_,
        glm::vec3{0.0F, 1.0F, 0.0F}
    );
    const auto pitch_rotation = glm::angleAxis(
        pitch_,
        glm::vec3{1.0F, 0.0F, 0.0F}
    );
    camera.set_orientation(yaw_rotation * pitch_rotation);

    glm::vec3 movement{0.0F};

    if (key_is_down(handle, GLFW_KEY_W)) {
        movement += camera.forward();
    }
    if (key_is_down(handle, GLFW_KEY_S)) {
        movement -= camera.forward();
    }
    if (key_is_down(handle, GLFW_KEY_D)) {
        movement += camera.right();
    }
    if (key_is_down(handle, GLFW_KEY_A)) {
        movement -= camera.right();
    }
    if (key_is_down(handle, GLFW_KEY_SPACE)) {
        movement += glm::vec3{0.0F, 1.0F, 0.0F};
    }
    if (key_is_down(handle, GLFW_KEY_LEFT_CONTROL)) {
        movement -= glm::vec3{0.0F, 1.0F, 0.0F};
    }

    const auto movement_length_squared = glm::dot(
        movement,
        movement
    );
    if (movement_length_squared == 0.0F) {
        return;
    }

    auto speed = movement_speed_;
    if (key_is_down(handle, GLFW_KEY_LEFT_SHIFT) ||
        key_is_down(handle, GLFW_KEY_RIGHT_SHIFT)) {
        speed *= fast_multiplier_;
    }

    camera.set_position(
        camera.position() +
        glm::normalize(movement) * speed * delta_time
    );
}
