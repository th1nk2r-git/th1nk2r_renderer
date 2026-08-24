#include "core/input/input_system.hpp"

#include <utility>

InputSystem::InputSystem(
    Window& window,
    Camera& camera,
    GeneralController::Callbacks general_callbacks
)
    : window_(window),
      general_controller_(std::move(general_callbacks)),
      camera_controller_(camera) {
    window_.set_key_callback(
        [this](int key, int action) {
            route_key(key, action);
        }
    );
    window_.set_cursor_position_callback(
        [this](double x, double y) {
            route_cursor(x, y);
        }
    );
    set_cursor_captured(true);
}

InputSystem::~InputSystem() noexcept {
    window_.set_key_callback({});
    window_.set_cursor_position_callback({});
}

auto InputSystem::route_key(int key, int action) -> void {
    if (general_controller_.handle_key(key, action)) {
        return;
    }

    const auto active = action != GLFW_RELEASE;

    switch (key) {
    case GLFW_KEY_W:
        camera_controller_.forward(active);
        break;
    case GLFW_KEY_S:
        camera_controller_.backward(active);
        break;
    case GLFW_KEY_A:
        camera_controller_.left(active);
        break;
    case GLFW_KEY_D:
        camera_controller_.right(active);
        break;
    case GLFW_KEY_SPACE:
        camera_controller_.up(active);
        break;
    case GLFW_KEY_LEFT_CONTROL:
        camera_controller_.down(active);
        break;
    case GLFW_KEY_LEFT_SHIFT:
        left_shift_active_ = active;
        camera_controller_.fast(
            left_shift_active_ || right_shift_active_
        );
        break;
    case GLFW_KEY_RIGHT_SHIFT:
        right_shift_active_ = active;
        camera_controller_.fast(
            left_shift_active_ || right_shift_active_
        );
        break;
    case GLFW_KEY_LEFT_ALT:
        left_alt_active_ = active;
        set_cursor_captured(
            !left_alt_active_ && !right_alt_active_
        );
        break;
    case GLFW_KEY_RIGHT_ALT:
        right_alt_active_ = active;
        set_cursor_captured(
            !left_alt_active_ && !right_alt_active_
        );
        break;
    default:
        break;
    }
}

auto InputSystem::route_cursor(double x, double y) -> void {
    if (cursor_captured_) {
        camera_controller_.look(x, y);
    }
}

auto InputSystem::set_cursor_captured(bool captured) -> void {
    if (cursor_captured_ == captured) {
        return;
    }

    auto* handle = window_.get();
    glfwSetInputMode(
        handle,
        GLFW_CURSOR,
        captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
    );
    if (captured && glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    else if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }

    cursor_captured_ = captured;
    camera_controller_.reset_look();
}
