#ifndef DATA_UPLOADER_HPP
#define DATA_UPLOADER_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/resource/buffer.hpp"
#include "gfx/resource/image.hpp"

struct BufferUploadDesc {
    vk::DeviceSize destination_offset = 0;
    vk::PipelineStageFlags destination_stage{};
    vk::AccessFlags destination_access{};
};

struct ImageUploadDesc {
    vk::Extent3D extent{};
    vk::Offset3D destination_offset{};
    vk::ImageSubresourceLayers subresource{
        vk::ImageAspectFlagBits::eColor,
        0,
        0,
        1
    };
    vk::ImageLayout final_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::PipelineStageFlags destination_stage =
        vk::PipelineStageFlagBits::eFragmentShader;
    vk::AccessFlags destination_access = vk::AccessFlagBits::eShaderRead;
};

class DataUploader {
public:
    DataUploader() noexcept = default;
    DataUploader(const Device& device, const GpuAllocator& allocator);

    DataUploader(const DataUploader&) = delete;
    auto operator=(const DataUploader&) -> DataUploader& = delete;

    DataUploader(DataUploader&& other) noexcept;
    auto operator=(DataUploader&& other) noexcept -> DataUploader&;

    // update the owners after the enclosing renderer has moved
    auto rebind(const Device& device, const GpuAllocator& allocator) noexcept -> void;

    // queue a CPU-to-GPU buffer upload
    // the destination buffer must include vk::BufferUsageFlagBits::eTransferDst
    auto enqueue_buffer(
        const void* data,
        vk::DeviceSize size,
        const Buffer& destination,
        const BufferUploadDesc& desc
    ) -> void;

    // queue a CPU-to-GPU image upload
    // the destination image must be newly created in eUndefined layout and
    // include vk::ImageUsageFlagBits::eTransferDst
    auto enqueue_image(
        std::span<const std::byte> data,
        const Image& destination,
        const ImageUploadDesc& desc
    ) -> void;

    // submit all queued uploads and wait for completion
    auto submit_and_wait() -> void;

    // return true if the pending uploads is empty
    auto empty() const noexcept -> bool {
        return pending_buffer_uploads_.empty() &&
               pending_image_uploads_.empty();
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

    struct PendingImageUpload {
        Buffer staging;
        vk::Image destination;
        vk::Extent3D extent{};
        vk::Offset3D destination_offset{};
        vk::ImageSubresourceLayers subresource{};
        vk::ImageLayout final_layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags destination_stage{};
        vk::AccessFlags destination_access{};
    };

    const Device* device_ = nullptr;
    const GpuAllocator* allocator_ = nullptr;
    std::vector<PendingBufferUpload> pending_buffer_uploads_;
    std::vector<PendingImageUpload> pending_image_uploads_;
};

#endif
