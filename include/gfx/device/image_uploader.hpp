#ifndef IMAGE_UPLOADER_HPP
#define IMAGE_UPLOADER_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "gfx/resource/image.hpp"

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
    vk::PipelineStageFlags destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
    vk::AccessFlags destination_access = vk::AccessFlagBits::eShaderRead;
    bool generate_mipmaps = false;
};

class ImageUploader {
public:
    ImageUploader(
        const Device& device,
        const MemoryAllocator& allocator
    );

    ImageUploader(const ImageUploader&) = delete;
    auto operator=(const ImageUploader&) -> ImageUploader& = delete;
    ImageUploader(ImageUploader&&) = delete;
    auto operator=(ImageUploader&&) -> ImageUploader& = delete;

    auto enqueue(
        std::span<const std::byte> data,
        const Image& destination,
        const ImageUploadDesc& desc
    ) -> void;

    auto submit_and_wait() -> void;

    auto empty() const noexcept -> bool {
        return pending_uploads_.empty();
    }

private:
    struct PendingUpload {
        Buffer staging;
        vk::Image destination;
        vk::Extent3D extent{};
        vk::Offset3D destination_offset{};
        vk::ImageSubresourceLayers subresource{};
        vk::ImageLayout final_layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags destination_stage{};
        vk::AccessFlags destination_access{};
        bool generate_mipmaps = false;
        uint32_t mip_levels = 1;
    };

    const Device& device_;
    const MemoryAllocator& allocator_;
    std::vector<PendingUpload> pending_uploads_;

    static auto record_mipmaps(
        vk::raii::CommandBuffer& command_buffer,
        const PendingUpload& upload
    ) -> void;
};

#endif
