#ifndef IMAGE_HPP
#define IMAGE_HPP

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

#include "gfx/device/memory_allocator.hpp"

struct ImageDesc {
    vk::ImageCreateFlags flags{};
    vk::ImageType type = vk::ImageType::e2D;
    vk::Format format = vk::Format::eUndefined;
    vk::Extent3D extent{};
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags usage{};
    vk::ImageLayout initial_layout = vk::ImageLayout::eUndefined;
};

class Image {
public:
    Image() = default;
    Image(const MemoryAllocator& allocator, const ImageDesc& desc);
    ~Image() noexcept;

    Image(const Image&) = delete;
    auto operator=(const Image&) -> Image& = delete;
    Image(Image&& other) noexcept;
    auto operator=(Image&& other) noexcept -> Image&;

    auto get() const noexcept -> vk::Image {
        return vk::Image{handle_};
    }

    auto type() const noexcept -> vk::ImageType {
        return type_;
    }

    auto format() const noexcept -> vk::Format {
        return format_;
    }

    auto extent() const noexcept -> vk::Extent3D {
        return extent_;
    }

    auto mip_levels() const noexcept -> uint32_t {
        return mip_levels_;
    }

    auto array_layers() const noexcept -> uint32_t {
        return array_layers_;
    }

    auto samples() const noexcept -> vk::SampleCountFlagBits {
        return samples_;
    }

    auto usage() const noexcept -> vk::ImageUsageFlags {
        return usage_;
    }

private:
    auto reset() noexcept -> void;

    VmaAllocator allocator_ = nullptr;
    VkImage handle_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;

    vk::ImageType type_ = vk::ImageType::e2D;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent3D extent_{};
    uint32_t mip_levels_ = 0;
    uint32_t array_layers_ = 0;
    vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1;
    vk::ImageUsageFlags usage_{};
};

#endif
