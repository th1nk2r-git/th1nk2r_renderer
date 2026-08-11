#ifndef CAMERA_CONTROLLER_HPP
#define CAMERA_CONTROLLER_HPP

#include "platform/window.hpp"
#include "scene/camera.hpp"

class CameraController {
public:
    auto enable(const Window& window) -> void;

    auto update(Camera& camera, const Window& window, float delta_time) -> void;

private:
    float movement_speed_ = 3.0F;
    float fast_multiplier_ = 3.0F;
    float mouse_sensitivity_ = 0.002F;

    float yaw_ = 0.0F;
    float pitch_ = 0.0F;

    double previous_cursor_x_ = 0.0;
    double previous_cursor_y_ = 0.0;
    bool first_cursor_sample_ = true;
    bool cursor_captured_ = false;

    auto set_cursor_captured(
        const Window& window,
        bool captured
    ) -> void;
};

#endif
