#include "gfx/resource/image.hpp"

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

    auto validate_image_desc(
        const MemoryAllocator& allocator,
        const ImageDesc& desc
    ) -> void {
        if (allocator.get() == nullptr) {
            throw std::invalid_argument(
                "image requires a valid GPU allocator!"
            );
        }

        if (desc.format == vk::Format::eUndefined) {
            throw std::invalid_argument(
                "image requires a defined format!"
            );
        }

        if (desc.extent.width == 0 ||
            desc.extent.height == 0 ||
            desc.extent.depth == 0) {
            throw std::invalid_argument(
                "image extent dimensions must be greater than zero!"
            );
        }

        if (desc.mip_levels == 0) {
            throw std::invalid_argument(
                "image requires at least one mip level!"
            );
        }

        if (desc.array_layers == 0) {
            throw std::invalid_argument(
                "image requires at least one array layer!"
            );
        }

        if (!desc.usage) {
            throw std::invalid_argument(
                "image requires at least one usage flag!"
            );
        }

        if (static_cast<VkSampleCountFlagBits>(desc.samples) == 0) {
            throw std::invalid_argument(
                "image requires a valid sample count!"
            );
        }

        if (desc.initial_layout != vk::ImageLayout::eUndefined &&
            desc.initial_layout != vk::ImageLayout::ePreinitialized) {
            throw std::invalid_argument(
                "image initial layout must be undefined or preinitialized!"
            );
        }

        if (desc.type == vk::ImageType::e1D &&
            (desc.extent.height != 1 || desc.extent.depth != 1)) {
            throw std::invalid_argument(
                "a 1D image requires height and depth to equal one!"
            );
        }

        if (desc.type == vk::ImageType::e2D && desc.extent.depth != 1) {
            throw std::invalid_argument(
                "a 2D image requires depth to equal one!"
            );
        }

        if (desc.type == vk::ImageType::e3D && desc.array_layers != 1) {
            throw std::invalid_argument(
                "a 3D image requires exactly one array layer!"
            );
        }
    }
}

Image::Image(const MemoryAllocator& allocator, const ImageDesc& desc) {
    validate_image_desc(allocator, desc);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.flags = static_cast<VkImageCreateFlags>(desc.flags);
    image_info.imageType = static_cast<VkImageType>(desc.type);
    image_info.format = static_cast<VkFormat>(desc.format);
    image_info.extent = VkExtent3D{
        desc.extent.width,
        desc.extent.height,
        desc.extent.depth
    };
    image_info.mipLevels = desc.mip_levels;
    image_info.arrayLayers = desc.array_layers;
    image_info.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    image_info.tiling = static_cast<VkImageTiling>(desc.tiling);
    image_info.usage = static_cast<VkImageUsageFlags>(desc.usage);
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = static_cast<VkImageLayout>(desc.initial_layout);

    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage new_handle = VK_NULL_HANDLE;
    VmaAllocation new_allocation = nullptr;

    const VkResult result = vmaCreateImage(
        allocator.get(),
        &image_info,
        &allocation_create_info,
        &new_handle,
        &new_allocation,
        nullptr
    );

    throw_if_vma_failed(result, "image creation");

    allocator_ = allocator.get();
    handle_ = new_handle;
    allocation_ = new_allocation;
    type_ = desc.type;
    format_ = desc.format;
    extent_ = desc.extent;
    mip_levels_ = desc.mip_levels;
    array_layers_ = desc.array_layers;
    samples_ = desc.samples;
    usage_ = desc.usage;
}

Image::~Image() noexcept {
    reset();
}

Image::Image(Image&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      type_(std::exchange(other.type_, vk::ImageType::e2D)),
      format_(std::exchange(other.format_, vk::Format::eUndefined)),
      extent_(std::exchange(other.extent_, vk::Extent3D{})),
      mip_levels_(std::exchange(other.mip_levels_, 0)),
      array_layers_(std::exchange(other.array_layers_, 0)),
      samples_(std::exchange(
          other.samples_,
          vk::SampleCountFlagBits::e1
      )),
      usage_(std::exchange(other.usage_, vk::ImageUsageFlags{})) {
}

auto Image::operator=(Image&& other) noexcept -> Image& {
    if (this == &other) {
        return *this;
    }

    reset();

    allocator_ = std::exchange(other.allocator_, nullptr);
    handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, nullptr);
    type_ = std::exchange(other.type_, vk::ImageType::e2D);
    format_ = std::exchange(other.format_, vk::Format::eUndefined);
    extent_ = std::exchange(other.extent_, vk::Extent3D{});
    mip_levels_ = std::exchange(other.mip_levels_, 0);
    array_layers_ = std::exchange(other.array_layers_, 0);
    samples_ = std::exchange(
        other.samples_,
        vk::SampleCountFlagBits::e1
    );
    usage_ = std::exchange(other.usage_, vk::ImageUsageFlags{});

    return *this;
}

auto Image::reset() noexcept -> void {
    if (handle_ != VK_NULL_HANDLE) {
        vmaDestroyImage(
            allocator_,
            handle_,
            allocation_
        );
    }

    allocator_ = nullptr;
    handle_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    type_ = vk::ImageType::e2D;
    format_ = vk::Format::eUndefined;
    extent_ = vk::Extent3D{};
    mip_levels_ = 0;
    array_layers_ = 0;
    samples_ = vk::SampleCountFlagBits::e1;
    usage_ = vk::ImageUsageFlags{};
}
