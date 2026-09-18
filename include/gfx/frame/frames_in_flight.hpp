#ifndef FRAMES_IN_FLIGHT_HPP
#define FRAMES_IN_FLIGHT_HPP

#include <cstdint>
#include <vector>

#include "gfx/device/device.hpp"

struct Frame {
    explicit Frame(const Device& device);

    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;
    Frame(Frame&&) noexcept = default;
    auto operator=(Frame&&) noexcept -> Frame& = default;

    auto reset_primary() -> void;

    vk::raii::CommandPool primary_command_pool = nullptr;
    vk::raii::CommandBuffer primary_command_buffer = nullptr;
    vk::raii::Semaphore image_available = nullptr;
    vk::raii::Fence in_flight_fence = nullptr;
};

class FramesInFlight {
public:
    explicit FramesInFlight(const Device& device);

    FramesInFlight(const FramesInFlight&) = delete;
    auto operator=(const FramesInFlight&) -> FramesInFlight& = delete;
    FramesInFlight(FramesInFlight&&) = delete;
    auto operator=(FramesInFlight&&) -> FramesInFlight& = delete;

    auto current_index() const noexcept -> uint32_t {
        return current_index_;
    }

    auto count() const noexcept -> uint32_t {
        return static_cast<uint32_t>(frames_.size());
    }

    auto current() noexcept -> Frame& {
        return frames_[current_index_];
    }

    auto current() const noexcept -> const Frame& {
        return frames_[current_index_];
    }

    auto wait_current() const -> void;
    auto reset_current_fence() const -> void;

    auto advance() noexcept -> void {
        current_index_ = (current_index_ + 1) % count();
    }

private:
    static constexpr uint32_t frame_count_ = 2;

    const Device& device_;
    uint32_t current_index_ = 0;
    std::vector<Frame> frames_;
};

#endif
