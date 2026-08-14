#include "gfx/device/buffer_uploader.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

BufferUploader::BufferUploader(const Device& device, const MemoryAllocator& allocator)
    : device_(device), allocator_(allocator) {}

auto BufferUploader::enqueue(
    const void* data,
    vk::DeviceSize size,
    const Buffer& destination,
    const BufferUploadDesc& desc
) -> void {
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
    if (!(destination.usage() & vk::BufferUsageFlagBits::eTransferDst)) {
        throw std::invalid_argument(
            "buffer upload destination requires transfer-dst usage!"
        );
    }
    if (desc.destination_offset > destination.size() ||
        size > destination.size() - desc.destination_offset) {
        throw std::out_of_range(
            "buffer upload exceeds the destination size!"
        );
    }
    if (!desc.destination_stage) {
        throw std::invalid_argument(
            "buffer upload requires a destination pipeline stage!"
        );
    }
    if (!desc.destination_access) {
        throw std::invalid_argument(
            "buffer upload requires a destination access mask!"
        );
    }

    Buffer staging{
        allocator_,
        BufferDesc{
            .size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .memory = BufferMemoryUsage::Upload
        }
    };
    staging.write(data, size);
    pending_uploads_.push_back(PendingUpload{
        .staging = std::move(staging),
        .destination = destination.get(),
        .destination_offset = desc.destination_offset,
        .size = size,
        .destination_stage = desc.destination_stage,
        .destination_access = desc.destination_access
    });
}

auto BufferUploader::submit_and_wait() -> void {
    if (empty()) {
        return;
    }

    const vk::CommandPoolCreateInfo command_pool_info{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = device_.graphics_family()
    };
    auto command_pool =
        device_.logical_device().createCommandPool(command_pool_info);
    const vk::CommandBufferAllocateInfo command_buffer_info{
        .commandPool = *command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    auto command_buffers =
        device_.logical_device().allocateCommandBuffers(command_buffer_info);
    auto& command_buffer = command_buffers.front();
    command_buffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    std::vector<vk::BufferMemoryBarrier> barriers;
    barriers.reserve(pending_uploads_.size());
    vk::PipelineStageFlags destination_stages{};

    for (const auto& upload : pending_uploads_) {
        command_buffer.copyBuffer(
            upload.staging.get(),
            upload.destination,
            vk::BufferCopy{
                .srcOffset = 0,
                .dstOffset = upload.destination_offset,
                .size = upload.size
            }
        );
        barriers.push_back(
            vk::BufferMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(upload.destination_access)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(upload.destination)
                .setOffset(upload.destination_offset)
                .setSize(upload.size)
        );
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
