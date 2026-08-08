#include "gfx/device/data_uploader.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "gfx/command/command_context.hpp"

namespace {
    auto mip_dimension(uint32_t dimension, uint32_t mip_level) -> uint32_t {
        while (mip_level > 0 && dimension > 1) {
            dimension /= 2;
            --mip_level;
        }

        return dimension;
    }

    auto validate_image_upload(
        const Image& destination,
        const ImageUploadDesc& desc) -> void {
        if (!destination.get()) {
            throw std::invalid_argument(
                "image upload requires a valid destination!"
            );
        }

        if (!(destination.usage() &
              vk::ImageUsageFlagBits::eTransferDst)) {
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
            desc.subresource.layerCount > destination.array_layers() - desc.subresource.baseArrayLayer) {
            throw std::out_of_range(
                "image upload array layers are out of range!"
            );
        }

        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
            throw std::invalid_argument(
                "image upload extent dimensions must be greater than zero!"
            );
        }

        if (desc.destination_offset.x < 0 || desc.destination_offset.y < 0 || desc.destination_offset.z < 0) {
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

        const auto offset_x = static_cast<uint32_t>(desc.destination_offset.x);
        const auto offset_y = static_cast<uint32_t>(desc.destination_offset.y);
        const auto offset_z = static_cast<uint32_t>(desc.destination_offset.z);

        if (offset_x > mip_extent.width ||
            desc.extent.width > mip_extent.width - offset_x ||
            offset_y > mip_extent.height ||
            desc.extent.height > mip_extent.height - offset_y ||
            offset_z > mip_extent.depth ||
            desc.extent.depth > mip_extent.depth - offset_z) {
            throw std::out_of_range(
                "image upload region exceeds the destination subresource!");
        }

        if (desc.final_layout == vk::ImageLayout::eUndefined ||
            desc.final_layout == vk::ImageLayout::ePreinitialized) {
            throw std::invalid_argument(
                "image upload requires a usable final layout!");
        }

        if (!desc.destination_stage) {
            throw std::invalid_argument(
                "image upload requires a destination pipeline stage!");
        }

        if (!desc.destination_access) {
            throw std::invalid_argument(
                "image upload requires a destination access mask!");
        }
    }

    auto subresource_range(
        const vk::ImageSubresourceLayers& subresource) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange {
            subresource.aspectMask,
            subresource.mipLevel,
            1,
            subresource.baseArrayLayer,
            subresource.layerCount
        };
    }
}

DataUploader::DataUploader(const Device& device, const GpuAllocator& allocator)
    : device_(&device), allocator_(&allocator) {}

auto DataUploader::enqueue_buffer(
    const void* data,
    vk::DeviceSize size,
    const Buffer& destination,
    const BufferUploadDesc& desc) -> void {
    if (!valid()) {
        throw std::logic_error(
            "data uploader is not initialized!");
    }

    if (size == 0) {
        return;
    }

    if (data == nullptr) {
        throw std::invalid_argument(
            "buffer upload source cannot be null!");
    }

    if (!destination.get()) {
        throw std::invalid_argument(
            "buffer upload requires a valid destination!");
    }

    if (desc.destination_offset > destination.size() ||
        size > destination.size() - desc.destination_offset) {
        throw std::out_of_range(
            "buffer upload exceeds the destination size!");
    }

    if (!desc.destination_stage) {
        throw std::invalid_argument(
            "buffer upload requires a destination pipeline stage!");
    }

    if (!desc.destination_access) {
        throw std::invalid_argument(
            "buffer upload requires a destination access mask!");
    }

    Buffer staging{
        *allocator_,
        BufferDesc{
            .size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .memory = BufferMemoryUsage::Upload
        }
    };

    staging.write(data, size);
    pending_buffer_uploads_.push_back(
        PendingBufferUpload{
            .staging = std::move(staging),
            .destination = destination.get(),
            .destination_offset = desc.destination_offset,
            .size = size,
            .destination_stage = desc.destination_stage,
            .destination_access = desc.destination_access
        }
    );
}

auto DataUploader::enqueue_image(
    std::span<const std::byte> data,
    const Image& destination,
    const ImageUploadDesc& desc) -> void {
    if (!valid()) {
        throw std::logic_error(
            "data uploader is not initialized!");
    }

    if (data.empty()) {
        return;
    }

    validate_image_upload(destination, desc);

    if (data.size() >
        static_cast<size_t>(
            std::numeric_limits<vk::DeviceSize>::max())) {
        throw std::length_error(
            "image upload source exceeds VkDeviceSize!");
    }

    const auto data_size = static_cast<vk::DeviceSize>(data.size());
    Buffer staging{
        *allocator_,
        BufferDesc{
            .size = data_size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .memory = BufferMemoryUsage::Upload
        }
    };
    staging.write(data.data(), data_size);

    pending_image_uploads_.push_back(
        PendingImageUpload{
            .staging = std::move(staging),
            .destination = destination.get(),
            .extent = desc.extent,
            .destination_offset = desc.destination_offset,
            .subresource = desc.subresource,
            .final_layout = desc.final_layout,
            .destination_stage = desc.destination_stage,
            .destination_access = desc.destination_access
        }
    );
}

auto DataUploader::submit_and_wait() -> void {
    if (!valid()) {
        throw std::logic_error(
            "data uploader is not initialized!");
    }

    if (empty()) {
        return;
    }

    CommandContext command_context{
        *device_,
        device_->graphics_family(),
        1};

    command_context.record(
        [&](vk::raii::CommandBuffer& command_buffer) {
            std::vector<vk::ImageMemoryBarrier> transfer_barriers;
            transfer_barriers.reserve(pending_image_uploads_.size());

            for (const auto& upload : pending_image_uploads_) {
                vk::ImageMemoryBarrier barrier{};
                barrier
                    .setSrcAccessMask({})
                    .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setOldLayout(vk::ImageLayout::eUndefined)
                    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(upload.destination)
                    .setSubresourceRange(
                        subresource_range(upload.subresource));
                transfer_barriers.push_back(barrier);
            }

            if (!transfer_barriers.empty()) {
                command_buffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eTransfer,
                    {},
                    {},
                    {},
                    transfer_barriers);
            }

            std::vector<vk::BufferMemoryBarrier> buffer_barriers;
            buffer_barriers.reserve(pending_buffer_uploads_.size());

            std::vector<vk::ImageMemoryBarrier> image_barriers;
            image_barriers.reserve(pending_image_uploads_.size());

            vk::PipelineStageFlags destination_stages{};

            for (const auto& upload : pending_buffer_uploads_) {
                vk::BufferCopy copy_region{};
                copy_region
                    .setSrcOffset(0)
                    .setDstOffset(upload.destination_offset)
                    .setSize(upload.size);

                command_buffer.copyBuffer(
                    upload.staging.get(),
                    upload.destination,
                    copy_region);

                vk::BufferMemoryBarrier barrier{};
                barrier
                    .setSrcAccessMask(
                        vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(upload.destination_access)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(upload.destination)
                    .setOffset(upload.destination_offset)
                    .setSize(upload.size);
                buffer_barriers.push_back(barrier);

                destination_stages |= upload.destination_stage;
            }

            for (const auto& upload : pending_image_uploads_) {
                vk::BufferImageCopy copy_region{};
                copy_region
                    .setBufferOffset(0)
                    .setBufferRowLength(0)
                    .setBufferImageHeight(0)
                    .setImageSubresource(upload.subresource)
                    .setImageOffset(upload.destination_offset)
                    .setImageExtent(upload.extent);

                command_buffer.copyBufferToImage(
                    upload.staging.get(),
                    upload.destination,
                    vk::ImageLayout::eTransferDstOptimal,
                    copy_region);

                vk::ImageMemoryBarrier barrier{};
                barrier
                    .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(upload.destination_access)
                    .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setNewLayout(upload.final_layout)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(upload.destination)
                    .setSubresourceRange(
                        subresource_range(upload.subresource));
                image_barriers.push_back(barrier);

                destination_stages |= upload.destination_stage;
            }

            command_buffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                destination_stages,
                {},
                {},
                buffer_barriers,
                image_barriers);
        }
    );

    const vk::CommandBuffer command_buffer = *command_context.command_buffers()[0];

    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(command_buffer);

    const vk::FenceCreateInfo fence_info{};
    auto fence = device_->logical_device().createFence(fence_info);

    device_->graphics_queue().submit(submit_info, fence);

    static_cast<void>(
        device_->logical_device().waitForFences(
            *fence,
            true,
            std::numeric_limits<uint64_t>::max()));

    pending_buffer_uploads_.clear();
    pending_image_uploads_.clear();
}
