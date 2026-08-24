#include "core/input/controller/general_controller.hpp"

#include <utility>

#include <GLFW/glfw3.h>

GeneralController::GeneralController(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

auto GeneralController::handle_key(int key, int action) const -> bool {
    if (key != GLFW_KEY_F || action != GLFW_PRESS) {
        return false;
    }

    if (callbacks_.toggle_fps) {
        callbacks_.toggle_fps();
    }
    return true;
}
