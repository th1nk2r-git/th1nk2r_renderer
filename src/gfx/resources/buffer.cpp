#include "gfx/resources/buffer.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace {
    auto throw_if_vma_failed(VkResult result, const char* operation) -> void {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                std::string(operation) +
                " failed, VkResult: " +
                std::to_string(static_cast<int>(result))
            );
        }
    }
}

Buffer::Buffer(const GpuAllocator& allocator, const BufferDesc& desc) {
    if (allocator.get() == nullptr) {
        throw std::invalid_argument(
            "buffer requires a valid GPU allocator!"
        );
    }

    if (desc.size == 0) {
        throw std::invalid_argument(
            "buffer size must be greater than zero!"
        );
    }

    if (static_cast<VkBufferUsageFlags>(desc.usage) == 0) {
        throw std::invalid_argument(
            "buffer requires at least one usage flag!"
        );
    }

    if (desc.persistent_mapping && desc.memory == BufferMemoryUsage::GpuOnly) {
        throw std::invalid_argument(
            "a GPU-only buffer cannot request persistent mapping!"
        );
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desc.size;
    buffer_info.usage = static_cast<VkBufferUsageFlags>(desc.usage);
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;

    switch (desc.memory) {
    case BufferMemoryUsage::GpuOnly:
        break;

    case BufferMemoryUsage::Upload:
        allocation_create_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;

    case BufferMemoryUsage::Readback:
        allocation_create_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        break;
    }

    if (desc.persistent_mapping) {
        allocation_create_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer new_handle = VK_NULL_HANDLE;
    VmaAllocation new_allocation = nullptr;
    VmaAllocationInfo allocation_info{};

    const VkResult result = vmaCreateBuffer(
        allocator.get(),
        &buffer_info,
        &allocation_create_info,
        &new_handle,
        &new_allocation,
        &allocation_info
    );

    throw_if_vma_failed(result, "buffer creation");

    allocator_ = allocator.get();
    handle_ = new_handle;
    allocation_ = new_allocation;
    size_ = desc.size;
    mapped_data_ = allocation_info.pMappedData;
}

Buffer::~Buffer() noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            allocator_,
            handle_,
            allocation_
        );
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      mapped_data_(std::exchange(other.mapped_data_, nullptr)) {
}

auto Buffer::operator=(Buffer&& other) noexcept -> Buffer& {
    if (this == &other) {
        return *this;
    }

    if (handle_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            allocator_,
            handle_,
            allocation_
        );
    }

    allocator_ = std::exchange(other.allocator_, nullptr);
    handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, nullptr);
    size_ = std::exchange(other.size_, 0);
    mapped_data_ = std::exchange(other.mapped_data_, nullptr);

    return *this;
}

auto Buffer::write(const void* data, vk::DeviceSize size,vk::DeviceSize offset) -> void {
    if (size == 0) {
        return;
    }

    if (data == nullptr) {
        throw std::invalid_argument(
            "buffer write source cannot be null"
        );
    }

    if (offset > size_ || size > size_ - offset) {
        throw std::out_of_range(
            "buffer write exceeds the buffer size"
        );
    }

    const VkResult result = vmaCopyMemoryToAllocation(
        allocator_,
        data,
        allocation_,
        offset,
        size
    );

    throw_if_vma_failed(result, "buffer write");
}
