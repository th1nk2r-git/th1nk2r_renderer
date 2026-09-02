#ifndef FRAMES_IN_FLIGHT_HPP
#define FRAMES_IN_FLIGHT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "gfx/device/device.hpp"

struct CommandRecordingSlot {
    explicit CommandRecordingSlot(const Device& device);

    CommandRecordingSlot(const CommandRecordingSlot&) = delete;
    auto operator=(const CommandRecordingSlot&) -> CommandRecordingSlot& = delete;
    CommandRecordingSlot(CommandRecordingSlot&&) noexcept = default;
    auto operator=(CommandRecordingSlot&&) noexcept -> CommandRecordingSlot& = default;

    template <typename Function>
    auto record(Function&& execute) -> void {
        const vk::CommandBufferBeginInfo begin_info{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        command_buffer.begin(begin_info);
        try {
            std::forward<Function>(execute)(command_buffer);
            command_buffer.end();
        }
        catch (...) {
            try {
                command_buffer.reset();
            }
            catch (...) {
            }
            throw;
        }
    }

    vk::raii::CommandPool command_pool = nullptr;
    vk::raii::CommandBuffer command_buffer = nullptr;
};

struct Frame {
    static constexpr std::size_t recording_slot_count = 2;

    explicit Frame(const Device& device);

    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;
    Frame(Frame&&) noexcept = default;
    auto operator=(Frame&&) noexcept -> Frame& = default;

    template <typename Function>
    auto record(std::size_t slot_index, Function&& execute) -> void {
        recording_slots_.at(slot_index).record(
            std::forward<Function>(execute)
        );
    }

    auto command_buffers() const noexcept
        -> std::array<vk::CommandBuffer, recording_slot_count>;

    vk::raii::Semaphore image_available = nullptr;
    vk::raii::Fence in_flight_fence = nullptr;

private:
    std::array<CommandRecordingSlot, recording_slot_count> recording_slots_;
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
