#ifndef BUFFER_UPLOADER_HPP
#define BUFFER_UPLOADER_HPP

#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"

struct BufferUploadDesc {
    vk::DeviceSize destination_offset = 0;
    vk::PipelineStageFlags destination_stage{};
    vk::AccessFlags destination_access{};
};

class BufferUploader {
public:
    BufferUploader(
        const Device& device,
        const MemoryAllocator& allocator
    );

    BufferUploader(const BufferUploader&) = delete;
    auto operator=(const BufferUploader&) -> BufferUploader& = delete;
    BufferUploader(BufferUploader&&) = delete;
    auto operator=(BufferUploader&&) -> BufferUploader& = delete;

    auto enqueue(
        const void* data,
        vk::DeviceSize size,
        const Buffer& destination,
        const BufferUploadDesc& desc
    ) -> void;

    auto submit_and_wait() -> void;

    auto empty() const noexcept -> bool {
        return pending_uploads_.empty();
    }

private:
    struct PendingUpload {
        Buffer staging;
        vk::Buffer destination;
        vk::DeviceSize destination_offset = 0;
        vk::DeviceSize size = 0;
        vk::PipelineStageFlags destination_stage{};
        vk::AccessFlags destination_access{};
    };

    const Device& device_;
    const MemoryAllocator& allocator_;
    std::vector<PendingUpload> pending_uploads_;
};

#endif
