#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>

class Timer {
public:
    explicit Timer(float max_delta_time = 0.05F);

    // Return elapsed seconds, capped at the maximum step, and advance the baseline.
    auto tick() noexcept -> float;

    // Discard elapsed time and start measuring from now.
    auto reset() noexcept -> void;

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previous_time_;
    float max_delta_time_;
};

#endif
