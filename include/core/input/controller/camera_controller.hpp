#ifndef CAMERA_CONTROLLER_HPP
#define CAMERA_CONTROLLER_HPP

#include "scene/camera.hpp"

class CameraController {
public:
    explicit CameraController(Camera& camera) noexcept;

    CameraController(const CameraController&) = delete;
    auto operator=(const CameraController&) -> CameraController& = delete;
    CameraController(CameraController&&) = delete;
    auto operator=(CameraController&&) -> CameraController& = delete;

    auto forward(bool active) noexcept -> void {
        forward_active_ = active;
    }

    auto backward(bool active) noexcept -> void {
        backward_active_ = active;
    }

    auto left(bool active) noexcept -> void {
        left_active_ = active;
    }

    auto right(bool active) noexcept -> void {
        right_active_ = active;
    }

    auto up(bool active) noexcept -> void {
        up_active_ = active;
    }

    auto down(bool active) noexcept -> void {
        down_active_ = active;
    }

    auto fast(bool active) noexcept -> void {
        fast_active_ = active;
    }

    auto look(double cursor_x, double cursor_y) -> void;

    auto reset_look() noexcept -> void {
        first_cursor_sample_ = true;
    }

    auto update(float delta_time) -> void;

private:
    Camera& camera_;

    float movement_speed_ = 3.0F;
    float fast_multiplier_ = 3.0F;
    float mouse_sensitivity_ = 0.002F;

    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    double previous_cursor_x_ = 0.0;
    double previous_cursor_y_ = 0.0;
    bool first_cursor_sample_ = true;

    bool forward_active_ = false;
    bool backward_active_ = false;
    bool left_active_ = false;
    bool right_active_ = false;
    bool up_active_ = false;
    bool down_active_ = false;
    bool fast_active_ = false;
};

#endif
