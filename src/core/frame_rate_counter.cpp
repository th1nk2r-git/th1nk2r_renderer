#include "core/frame_rate_counter.hpp"

auto FrameRateCounter::begin_frame() noexcept -> void {
    if (!sampling_) {
        sample_start_ = Clock::now();
        sampling_ = true;
    }
    frame_in_progress_ = true;
}

auto FrameRateCounter::end_frame() noexcept -> bool {
    if (!frame_in_progress_) {
        return false;
    }
    frame_in_progress_ = false;
    ++frame_count_;

    const auto sample_end = Clock::now();
    const auto sample_duration = sample_end - sample_start_;
    if (sample_duration < update_interval_) {
        return false;
    }

    average_fps_ = static_cast<double>(frame_count_) / std::chrono::duration<double>(sample_duration).count();
    sample_start_ = sample_end;
    frame_count_ = 0;
    return true;
}

auto FrameRateCounter::reset() noexcept -> void {
    sample_start_ = {};
    frame_count_ = 0;
    average_fps_ = 0.0;
    sampling_ = false;
    frame_in_progress_ = false;
}
