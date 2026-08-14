#ifndef FRAMES_IN_FLIGHT_HPP
#define FRAMES_IN_FLIGHT_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "gfx/device/device.hpp"

struct Frame {
    explicit Frame(const Device& device);

    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;
    Frame(Frame&&) noexcept = default;
    auto operator=(Frame&&) noexcept -> Frame& = default;

    template <typename Function>
    auto record(Function&& execute) -> void {
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        command_buffer.begin(begin_info);
        execute(command_buffer);
        command_buffer.end();
    }

    vk::raii::CommandPool command_pool = nullptr;
    vk::raii::CommandBuffer command_buffer = nullptr;
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

    auto current_frame_index() const noexcept -> uint32_t {
        return current_frame_index_;
    }

    auto frame_count() const noexcept -> uint32_t {
        return static_cast<uint32_t>(frames_.size());
    }

    auto current_frame() noexcept -> Frame& {
        return frames_[current_frame_index_];
    }

    auto current_frame() const noexcept -> const Frame& {
        return frames_[current_frame_index_];
    }

    auto wait(const Device& device) -> void;
    auto reset(const Device& device) -> void;

    auto advance() noexcept -> void {
        current_frame_index_ = (current_frame_index_ + 1) % static_cast<uint32_t>(frames_.size());
    }

private:
    static constexpr uint32_t max_frames_in_flight_ = 2;
    uint32_t current_frame_index_ = 0;
    std::vector<Frame> frames_;
};

#endif
