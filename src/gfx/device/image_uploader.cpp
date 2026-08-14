#include "gfx/device/image_uploader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    auto mip_dimension(uint32_t dimension, uint32_t mip_level) -> uint32_t {
        while (mip_level > 0 && dimension > 1) {
            dimension /= 2;
            --mip_level;
        }
        return dimension;
    }

    auto subresource_range(const vk::ImageSubresourceLayers& subresource) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange{
            subresource.aspectMask,
            subresource.mipLevel,
            1,
            subresource.baseArrayLayer,
            subresource.layerCount
        };
    }

    auto mip_subresource_range(const vk::ImageSubresourceLayers& subresource, uint32_t mip_level) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange{
            subresource.aspectMask,
            mip_level,
            1,
            subresource.baseArrayLayer,
            subresource.layerCount
        };
    }

    auto validate_upload(const Device& device, const Image& destination, const ImageUploadDesc& desc) -> void {
        if (!destination.get()) {
            throw std::invalid_argument(
                "image upload requires a valid destination!"
            );
        }
        if (!(destination.usage() & vk::ImageUsageFlagBits::eTransferDst)) {
            throw std::invalid_argument(
                "image upload destination requires transfer-dst usage!"
            );
        }
        if (!desc.subresource.aspectMask) {
            throw std::invalid_argument(
                "image upload requires an aspect mask!"
            );
        }
        if (desc.subresource.mipLevel >= destination.mip_levels()) {
            throw std::out_of_range(
                "image upload mip level is out of range!"
            );
        }
        if (desc.subresource.layerCount == 0 ||
            desc.subresource.baseArrayLayer >= destination.array_layers() ||
            desc.subresource.layerCount >
                destination.array_layers() -
                    desc.subresource.baseArrayLayer) {
            throw std::out_of_range(
                "image upload array layers are out of range!"
            );
        }
        if (desc.extent.width == 0 ||
            desc.extent.height == 0 ||
            desc.extent.depth == 0) {
            throw std::invalid_argument(
                "image upload extent dimensions must be greater than zero!"
            );
        }
        if (desc.destination_offset.x < 0 ||
            desc.destination_offset.y < 0 ||
            desc.destination_offset.z < 0) {
            throw std::invalid_argument(
                "image upload destination offset cannot be negative!"
            );
        }

        const auto destination_extent = destination.extent();
        const auto mip_level = desc.subresource.mipLevel;
        const vk::Extent3D mip_extent{
            mip_dimension(destination_extent.width, mip_level),
            mip_dimension(destination_extent.height, mip_level),
            mip_dimension(destination_extent.depth, mip_level)
        };
        const auto offset_x =
            static_cast<uint32_t>(desc.destination_offset.x);
        const auto offset_y =
            static_cast<uint32_t>(desc.destination_offset.y);
        const auto offset_z =
            static_cast<uint32_t>(desc.destination_offset.z);
        if (offset_x > mip_extent.width ||
            desc.extent.width > mip_extent.width - offset_x ||
            offset_y > mip_extent.height ||
            desc.extent.height > mip_extent.height - offset_y ||
            offset_z > mip_extent.depth ||
            desc.extent.depth > mip_extent.depth - offset_z) {
            throw std::out_of_range(
                "image upload region exceeds the destination subresource!"
            );
        }
        if (desc.final_layout == vk::ImageLayout::eUndefined ||
            desc.final_layout == vk::ImageLayout::ePreinitialized) {
            throw std::invalid_argument(
                "image upload requires a usable final layout!"
            );
        }
        if (!desc.destination_stage) {
            throw std::invalid_argument(
                "image upload requires a destination pipeline stage!"
            );
        }
        if (!desc.destination_access) {
            throw std::invalid_argument(
                "image upload requires a destination access mask!"
            );
        }

        if (!desc.generate_mipmaps) {
            return;
        }
        if (!(destination.usage() & vk::ImageUsageFlagBits::eTransferSrc)) {
            throw std::invalid_argument(
                "mipmap generation requires transfer-src usage!"
            );
        }
        if (destination.mip_levels() < 2) {
            throw std::invalid_argument(
                "mipmap generation requires more than one mip level!"
            );
        }
        if (desc.subresource.mipLevel != 0 ||
            desc.destination_offset != vk::Offset3D{} ||
            desc.extent != destination.extent()) {
            throw std::invalid_argument(
                "mipmap generation requires a full level-zero upload!"
            );
        }
        if (desc.extent.depth != 1) {
            throw std::invalid_argument(
                "mipmap generation currently supports only 2D images!"
            );
        }
        if (destination.type() != vk::ImageType::e2D) {
            throw std::invalid_argument(
                "mipmap generation requires a 2D image!"
            );
        }
        if (destination.samples() != vk::SampleCountFlagBits::e1) {
            throw std::invalid_argument(
                "mipmap generation requires a single-sampled image!"
            );
        }
        if (desc.subresource.aspectMask != vk::ImageAspectFlagBits::eColor) {
            throw std::invalid_argument(
                "linear mipmap generation requires the color aspect!"
            );
        }

        const auto format_features = device.physical_device().getFormatProperties(destination.format()).optimalTilingFeatures;
        const auto required_features =
            vk::FormatFeatureFlagBits::eBlitSrc |
            vk::FormatFeatureFlagBits::eBlitDst |
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((format_features & required_features) != required_features) {
            throw std::runtime_error(
                "image format does not support linear mipmap blits!"
            );
        }
    }

}

ImageUploader::ImageUploader(
    const Device& device,
    const MemoryAllocator& allocator
) : device_(device), allocator_(allocator) {}

auto ImageUploader::record_mipmaps(
    vk::raii::CommandBuffer& command_buffer,
    const PendingUpload& upload
) -> void {
    auto mip_width = static_cast<int32_t>(upload.extent.width);
    auto mip_height = static_cast<int32_t>(upload.extent.height);

    for (uint32_t mip_level = 1;
         mip_level < upload.mip_levels;
         ++mip_level) {
        const auto source_mip_level = mip_level - 1;

        vk::ImageMemoryBarrier source_barrier{};
        source_barrier
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(upload.destination)
            .setSubresourceRange(
                mip_subresource_range(
                    upload.subresource,
                    source_mip_level
                )
            );

        vk::ImageMemoryBarrier destination_barrier{};
        destination_barrier
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(upload.destination)
            .setSubresourceRange(
                mip_subresource_range(upload.subresource, mip_level)
            );

        const std::array prepare_barriers{
            source_barrier,
            destination_barrier
        };
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            {},
            {},
            prepare_barriers
        );

        const auto next_width = std::max(mip_width / 2, 1);
        const auto next_height = std::max(mip_height / 2, 1);
        vk::ImageBlit blit{};
        blit.srcSubresource = vk::ImageSubresourceLayers{
            upload.subresource.aspectMask,
            source_mip_level,
            upload.subresource.baseArrayLayer,
            upload.subresource.layerCount
        };
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{mip_width, mip_height, 1};
        blit.dstSubresource = vk::ImageSubresourceLayers{
            upload.subresource.aspectMask,
            mip_level,
            upload.subresource.baseArrayLayer,
            upload.subresource.layerCount
        };
        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{next_width, next_height, 1};

        command_buffer.blitImage(
            upload.destination,
            vk::ImageLayout::eTransferSrcOptimal,
            upload.destination,
            vk::ImageLayout::eTransferDstOptimal,
            blit,
            vk::Filter::eLinear
        );

        vk::ImageMemoryBarrier source_final_barrier{};
        source_final_barrier
            .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
            .setDstAccessMask(upload.destination_access)
            .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setNewLayout(upload.final_layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(upload.destination)
            .setSubresourceRange(
                mip_subresource_range(
                    upload.subresource,
                    source_mip_level
                )
            );
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            upload.destination_stage,
            {},
            {},
            {},
            source_final_barrier
        );

        mip_width = next_width;
        mip_height = next_height;
    }

    vk::ImageMemoryBarrier last_mip_barrier{};
    last_mip_barrier
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(upload.destination_access)
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(upload.final_layout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(upload.destination)
        .setSubresourceRange(
            mip_subresource_range(
                upload.subresource,
                upload.mip_levels - 1
            )
        );
    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        upload.destination_stage,
        {},
        {},
        {},
        last_mip_barrier
    );
}

auto ImageUploader::enqueue(
    std::span<const std::byte> data,
    const Image& destination,
    const ImageUploadDesc& desc
) -> void {
    if (data.empty()) {
        return;
    }
    validate_upload(device_, destination, desc);
    if (data.size() >
        static_cast<size_t>(std::numeric_limits<vk::DeviceSize>::max())) {
        throw std::length_error(
            "image upload source exceeds VkDeviceSize!"
        );
    }

    const auto data_size = static_cast<vk::DeviceSize>(data.size());
    Buffer staging{
        allocator_,
        BufferDesc{
            .size = data_size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .memory = BufferMemoryUsage::Upload
        }
    };
    staging.write(data.data(), data_size);
    pending_uploads_.push_back(PendingUpload{
        .staging = std::move(staging),
        .destination = destination.get(),
        .extent = desc.extent,
        .destination_offset = desc.destination_offset,
        .subresource = desc.subresource,
        .final_layout = desc.final_layout,
        .destination_stage = desc.destination_stage,
        .destination_access = desc.destination_access,
        .generate_mipmaps = desc.generate_mipmaps,
        .mip_levels = destination.mip_levels()
    });
}

auto ImageUploader::submit_and_wait() -> void {
    if (empty()) {
        return;
    }

    const vk::CommandPoolCreateInfo command_pool_info{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = device_.graphics_family()
    };
    auto command_pool = device_.logical_device().createCommandPool(command_pool_info);
    const vk::CommandBufferAllocateInfo command_buffer_info{
        .commandPool = *command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    auto command_buffers = device_.logical_device().allocateCommandBuffers(command_buffer_info);
    auto& command_buffer = command_buffers.front();
    command_buffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });
    std::vector<vk::ImageMemoryBarrier> transfer_barriers;
    transfer_barriers.reserve(pending_uploads_.size());
    for (const auto& upload : pending_uploads_) {
        transfer_barriers.push_back(
            vk::ImageMemoryBarrier{}
                .setSrcAccessMask({})
                .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(upload.destination)
                .setSubresourceRange(subresource_range(upload.subresource))
        );
    }
    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        {},
        {},
        transfer_barriers
    );

    std::vector<vk::ImageMemoryBarrier> final_barriers;
    final_barriers.reserve(pending_uploads_.size());
    vk::PipelineStageFlags destination_stages{};

    for (const auto& upload : pending_uploads_) {
        command_buffer.copyBufferToImage(
            upload.staging.get(),
            upload.destination,
            vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy{}
                .setBufferOffset(0)
                .setBufferRowLength(0)
                .setBufferImageHeight(0)
                .setImageSubresource(upload.subresource)
                .setImageOffset(upload.destination_offset)
                .setImageExtent(upload.extent)
        );

        if (upload.generate_mipmaps) {
            record_mipmaps(command_buffer, upload);
            continue;
        }

        final_barriers.push_back(
            vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(upload.destination_access)
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(upload.final_layout)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(upload.destination)
                .setSubresourceRange(subresource_range(upload.subresource))
        );
        destination_stages |= upload.destination_stage;
    }

    if (!final_barriers.empty()) {
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            destination_stages,
            {},
            {},
            {},
            final_barriers
        );
    }
    command_buffer.end();

    const vk::CommandBuffer command_buffer_handle = *command_buffer;
    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(command_buffer_handle);

    auto fence = device_.logical_device().createFence(
        vk::FenceCreateInfo{}
    );
    device_.graphics_queue().submit(submit_info, fence);
    static_cast<void>(device_.logical_device().waitForFences(
        *fence,
        true,
        std::numeric_limits<uint64_t>::max()
    ));
    pending_uploads_.clear();
}
