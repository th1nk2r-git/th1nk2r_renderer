#include "gfx/resource/image.hpp"

#include "gfx/device/device.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
    std::atomic_uint64_t next_image_id{1};

    auto throw_if_vma_failed(VkResult result, const char* operation) -> void {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                std::string(operation) +
                " failed, VkResult: " +
                std::to_string(static_cast<int>(result))
            );
        }
    }

    auto validate_image_desc(const ImageDesc& desc) -> void {
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

    auto image_aspects(vk::Format format) -> vk::ImageAspectFlags {
        switch (format) {
        case vk::Format::eD16Unorm:
        case vk::Format::eX8D24UnormPack32:
        case vk::Format::eD32Sfloat:
            return vk::ImageAspectFlagBits::eDepth;
        case vk::Format::eS8Uint:
            return vk::ImageAspectFlagBits::eStencil;
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth |
                vk::ImageAspectFlagBits::eStencil;
        default:
            return vk::ImageAspectFlagBits::eColor;
        }
    }

    auto default_view_type(const ImageDesc& desc) -> vk::ImageViewType {
        switch (desc.type) {
        case vk::ImageType::e1D:
            return desc.array_layers > 1
                ? vk::ImageViewType::e1DArray
                : vk::ImageViewType::e1D;
        case vk::ImageType::e2D:
            if (desc.flags & vk::ImageCreateFlagBits::eCubeCompatible) {
                return desc.array_layers > 6
                    ? vk::ImageViewType::eCubeArray
                    : vk::ImageViewType::eCube;
            }
            return desc.array_layers > 1
                ? vk::ImageViewType::e2DArray
                : vk::ImageViewType::e2D;
        case vk::ImageType::e3D:
            return vk::ImageViewType::e3D;
        }
        throw std::logic_error("unknown image type!");
    }

    auto needs_default_view(vk::ImageUsageFlags usage) -> bool {
        constexpr auto view_usages =
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eDepthStencilAttachment |
            vk::ImageUsageFlagBits::eInputAttachment;
        return static_cast<bool>(usage & view_usages);
    }

    auto create_default_view(
        const Device& device,
        vk::Image image,
        const ImageDesc& desc
    ) -> vk::raii::ImageView {
        if (!needs_default_view(desc.usage)) {
            return nullptr;
        }

        vk::ImageViewCreateInfo create_info{};
        create_info
            .setImage(image)
            .setViewType(default_view_type(desc))
            .setFormat(desc.format)
            .setComponents(vk::ComponentMapping{
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity
            })
            .setSubresourceRange(vk::ImageSubresourceRange{
                image_aspects(desc.format),
                0,
                desc.mip_levels,
                0,
                desc.array_layers
            });
        return device.logical_device().createImageView(create_info);
    }
}

Image::Image(
    const Device& device,
    const MemoryAllocator& allocator,
    const ImageDesc& desc
) {
    if (allocator.get() == nullptr) {
        throw std::invalid_argument(
            "image requires a valid GPU allocator!"
        );
    }
    validate_image_desc(desc);
    if (desc.initial_layout != vk::ImageLayout::eUndefined &&
        desc.initial_layout != vk::ImageLayout::ePreinitialized) {
        throw std::invalid_argument(
            "owned image initial layout must be undefined or preinitialized!"
        );
    }

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
    view_ = create_default_view(device, vk::Image{handle_}, desc);
    id_ = next_image_id.fetch_add(1, std::memory_order_relaxed);
    type_ = desc.type;
    format_ = desc.format;
    extent_ = desc.extent;
    mip_levels_ = desc.mip_levels;
    array_layers_ = desc.array_layers;
    samples_ = desc.samples;
    usage_ = desc.usage;
    initial_layout_ = desc.initial_layout;
    final_layout_ = desc.final_layout;
}

Image::Image(
    const Device& device,
    vk::Image handle,
    const ImageDesc& desc
) {
    validate_image_desc(desc);
    if (!handle) {
        throw std::invalid_argument("external image handle cannot be null!");
    }

    handle_ = static_cast<VkImage>(handle);
    view_ = create_default_view(device, handle, desc);
    id_ = next_image_id.fetch_add(1, std::memory_order_relaxed);
    external_ = true;
    type_ = desc.type;
    format_ = desc.format;
    extent_ = desc.extent;
    mip_levels_ = desc.mip_levels;
    array_layers_ = desc.array_layers;
    samples_ = desc.samples;
    usage_ = desc.usage;
    initial_layout_ = desc.initial_layout;
    final_layout_ = desc.final_layout;
}

auto Image::external(
    const Device& device,
    vk::Image handle,
    const ImageDesc& desc
) -> Image {
    return Image{device, handle, desc};
}

Image::~Image() noexcept {
    reset();
}

Image::Image(Image&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      view_(std::move(other.view_)),
      id_(std::exchange(other.id_, 0)),
      external_(std::exchange(other.external_, false)),
      type_(std::exchange(other.type_, vk::ImageType::e2D)),
      format_(std::exchange(other.format_, vk::Format::eUndefined)),
      extent_(std::exchange(other.extent_, vk::Extent3D{})),
      mip_levels_(std::exchange(other.mip_levels_, 0)),
      array_layers_(std::exchange(other.array_layers_, 0)),
      samples_(std::exchange(
          other.samples_,
          vk::SampleCountFlagBits::e1
      )),
      usage_(std::exchange(other.usage_, vk::ImageUsageFlags{})),
      initial_layout_(std::exchange(
          other.initial_layout_,
          vk::ImageLayout::eUndefined
      )),
      final_layout_(std::exchange(
          other.final_layout_,
          vk::ImageLayout::eUndefined
      )) {
}

auto Image::operator=(Image&& other) noexcept -> Image& {
    if (this == &other) {
        return *this;
    }

    reset();

    allocator_ = std::exchange(other.allocator_, nullptr);
    handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, nullptr);
    view_ = std::move(other.view_);
    id_ = std::exchange(other.id_, 0);
    external_ = std::exchange(other.external_, false);
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
    initial_layout_ = std::exchange(
        other.initial_layout_,
        vk::ImageLayout::eUndefined
    );
    final_layout_ = std::exchange(
        other.final_layout_,
        vk::ImageLayout::eUndefined
    );

    return *this;
}

auto Image::reset() noexcept -> void {
    view_ = nullptr;

    if (allocation_ != nullptr) {
        vmaDestroyImage(
            allocator_,
            handle_,
            allocation_
        );
    }

    allocator_ = nullptr;
    handle_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    id_ = 0;
    external_ = false;
    type_ = vk::ImageType::e2D;
    format_ = vk::Format::eUndefined;
    extent_ = vk::Extent3D{};
    mip_levels_ = 0;
    array_layers_ = 0;
    samples_ = vk::SampleCountFlagBits::e1;
    usage_ = vk::ImageUsageFlags{};
    initial_layout_ = vk::ImageLayout::eUndefined;
    final_layout_ = vk::ImageLayout::eUndefined;
}
