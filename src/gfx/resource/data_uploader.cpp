#include "gfx/resource/data_uploader.hpp"

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gfx/command/command_context.hpp"

DataUploader::DataUploader(const Device& device, const GpuAllocator& allocator)
    : device_(&device), allocator_(&allocator) {}

DataUploader::DataUploader(DataUploader&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      allocator_(std::exchange(other.allocator_, nullptr)),
      pending_uploads_(std::move(other.pending_uploads_)) {}

auto DataUploader::operator=(DataUploader&& other) noexcept -> DataUploader& {
    if (this == &other) {
        return *this;
    }

    pending_uploads_ = std::move(other.pending_uploads_);
    device_ = std::exchange(other.device_, nullptr);
    allocator_ = std::exchange(other.allocator_, nullptr);
    return *this;
}

auto DataUploader::rebind(
    const Device& device,
    const GpuAllocator& allocator
) noexcept -> void {
    device_ = &device;
    allocator_ = &allocator;
}

auto DataUploader::enqueue(
    const void* data,
    vk::DeviceSize size,
    const Buffer& destination,
    vk::PipelineStageFlags destination_stage,
    vk::AccessFlags destination_access,
    vk::DeviceSize destination_offset
) -> void {
    if (!valid()) {
        throw std::logic_error(
            "data uploader is not initialized!"
        );
    }

    if (size == 0) {
        return;
    }

    if (data == nullptr) {
        throw std::invalid_argument(
            "buffer upload source cannot be null!"
        );
    }

    if (!destination.get()) {
        throw std::invalid_argument(
            "buffer upload requires a valid destination!"
        );
    }

    if (destination_offset > destination.size() ||
        size > destination.size() - destination_offset) {
        throw std::out_of_range(
            "buffer upload exceeds the destination size!"
        );
    }

    if (!destination_stage) {
        throw std::invalid_argument(
            "buffer upload requires a destination pipeline stage!"
        );
    }

    if (!destination_access) {
        throw std::invalid_argument(
            "buffer upload requires a destination access mask!"
        );
    }

    Buffer staging {
        *allocator_,
        BufferDesc{
            .size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .memory = BufferMemoryUsage::Upload
        }
    };
    staging.write(data, size);

    pending_uploads_.push_back(
        PendingBufferUpload{
            .staging = std::move(staging),
            .destination = destination.get(),
            .destination_offset = destination_offset,
            .size = size,
            .destination_stage = destination_stage,
            .destination_access = destination_access
        }
    );
}

auto DataUploader::submit_and_wait() -> void {
    if (!valid()) {
        throw std::logic_error(
            "data uploader is not initialized!"
        );
    }

    if (pending_uploads_.empty()) {
        return;
    }

    CommandContext command_context{
        *device_,
        device_->graphics_family(),
        1
    };

    command_context.record(
        [&](vk::raii::CommandBuffer& command_buffer) {
            std::vector<vk::BufferMemoryBarrier> barriers;
            barriers.reserve(pending_uploads_.size());

            vk::PipelineStageFlags destination_stages{};

            for (const auto& upload : pending_uploads_) {
                vk::BufferCopy copy_region{};
                copy_region
                    .setSrcOffset(0)
                    .setDstOffset(upload.destination_offset)
                    .setSize(upload.size);

                command_buffer.copyBuffer(
                    upload.staging.get(),
                    upload.destination,
                    copy_region
                );

                vk::BufferMemoryBarrier barrier{};
                barrier
                    .setSrcAccessMask(
                        vk::AccessFlagBits::eTransferWrite
                    )
                    .setDstAccessMask(upload.destination_access)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(upload.destination)
                    .setOffset(upload.destination_offset)
                    .setSize(upload.size);
                barriers.push_back(barrier);

                destination_stages |= upload.destination_stage;
            }

            command_buffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                destination_stages,
                {},
                {},
                barriers,
                {}
            );
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
            std::numeric_limits<uint64_t>::max()
        )
    );

    pending_uploads_.clear();
}
