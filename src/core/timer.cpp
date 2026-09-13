#include "core/timer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

Timer::Timer(float max_delta_time)
    : previous_time_(Clock::now()), max_delta_time_(max_delta_time) {
    if (!std::isfinite(max_delta_time_) || max_delta_time_ <= 0.0F) {
        throw std::invalid_argument("maximum delta time must be finite and positive");
    }
}

auto Timer::tick() noexcept -> float {
    const auto current_time = Clock::now();
    const auto delta_time = std::chrono::duration<float>(
        current_time - previous_time_
    ).count();
    previous_time_ = current_time;
    return std::min(delta_time, max_delta_time_);
}

auto Timer::reset() noexcept -> void {
    previous_time_ = Clock::now();
}
