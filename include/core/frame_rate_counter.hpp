#ifndef FRAME_RATE_COUNTER_HPP
#define FRAME_RATE_COUNTER_HPP

#include <chrono>
#include <cstdint>

class FrameRateCounter {
public:
    // begin timing a frame and start a new sample window when necessary
    auto begin_frame() noexcept -> void;

    // finish a frame; returns true when the one-second average was updated
    auto end_frame() noexcept -> bool;

    auto average_fps() const noexcept -> double {
        return average_fps_;
    }

    // discard the current sample window
    auto reset() noexcept -> void;

private:
    using Clock = std::chrono::steady_clock;

    static constexpr auto update_interval_ = std::chrono::seconds{3};

    Clock::time_point sample_start_{};
    uint32_t frame_count_ = 0;
    double average_fps_ = 0.0;
    bool sampling_ = false;
    bool frame_in_progress_ = false;
};

#endif
