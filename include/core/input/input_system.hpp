#ifndef INPUT_SYSTEM_HPP
#define INPUT_SYSTEM_HPP

#include "core/input/controller/camera_controller.hpp"
#include "core/input/controller/general_controller.hpp"
#include "platform/window.hpp"

class InputSystem {
public:
    InputSystem(
        Window& window,
        Camera& camera,
        GeneralController::Callbacks general_callbacks
    );
    ~InputSystem() noexcept;

    InputSystem(const InputSystem&) = delete;
    auto operator=(const InputSystem&) -> InputSystem& = delete;
    InputSystem(InputSystem&&) = delete;
    auto operator=(InputSystem&&) -> InputSystem& = delete;

    auto update(float delta_time) -> void {
        if (cursor_captured_) {
            camera_controller_.update(delta_time);
        }
    }

private:
    Window& window_;
    GeneralController general_controller_;
    CameraController camera_controller_;

    bool cursor_captured_ = false;
    bool left_alt_active_ = false;
    bool right_alt_active_ = false;
    bool left_shift_active_ = false;
    bool right_shift_active_ = false;

    auto route_key(int key, int action) -> void;
    auto route_cursor(double x, double y) -> void;
    auto set_cursor_captured(bool captured) -> void;
};

#endif
