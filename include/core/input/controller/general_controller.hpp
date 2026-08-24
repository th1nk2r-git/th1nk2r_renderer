#ifndef GENERAL_CONTROLLER_HPP
#define GENERAL_CONTROLLER_HPP

#include <functional>

class GeneralController {
public:
    struct Callbacks {
        std::function<void()> toggle_fps;
    };

    explicit GeneralController(Callbacks callbacks);

    GeneralController(const GeneralController&) = delete;
    auto operator=(const GeneralController&) -> GeneralController& = delete;
    GeneralController(GeneralController&&) = delete;
    auto operator=(GeneralController&&) -> GeneralController& = delete;

    auto handle_key(int key, int action) const -> bool;

private:
    Callbacks callbacks_;
};

#endif
