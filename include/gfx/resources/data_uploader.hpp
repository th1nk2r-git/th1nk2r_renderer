#ifndef DATA_UPLOADER_HPP
#define DATA_UPLOADER_HPP

#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/resources/buffer.hpp"

class DataUploader {
public:
    DataUploader() noexcept = default;
    DataUploader(const Device& device, const GpuAllocator& allocator);

    DataUploader(const DataUploader&) = delete;
    auto operator=(const DataUploader&) -> DataUploader& = delete;

    DataUploader(DataUploader&&) noexcept = default;
    auto operator=(DataUploader&&) noexcept -> DataUploader& = default;

    // queue a CPU-to-GPU buffer upload
    // the destination buffer must include vk::BufferUsageFlagBits::eTransferDst
    auto enqueue(
        const void* data,
        vk::DeviceSize size,
        const Buffer& destination,
        vk::PipelineStageFlags destination_stage,
        vk::AccessFlags destination_access,
        vk::DeviceSize destination_offset = 0
    ) -> void;

    // submit all queued uploads and wait for completion
    auto submit_and_wait() -> void;

    // return true if the pending uploads is empty
    auto empty() const noexcept -> bool {
        return pending_uploads_.empty();
    }

    // return true if the data uploader is valid
    auto valid() const noexcept -> bool {
        return device_ != nullptr && allocator_ != nullptr;
    }

private:
    struct PendingBufferUpload {
        Buffer staging;
        vk::Buffer destination;
        vk::DeviceSize destination_offset = 0;
        vk::DeviceSize size = 0;
        vk::PipelineStageFlags destination_stage{};
        vk::AccessFlags destination_access{};
    };

    const Device* device_ = nullptr;
    const GpuAllocator* allocator_ = nullptr;
    std::vector<PendingBufferUpload> pending_uploads_;
};

#endif
