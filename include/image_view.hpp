#ifndef IMAGE_VIEW_HPP
#define IMAGE_VIEW_HPP

#include <vulkan/vulkan_raii.hpp>

class ImageView {
public:
    ImageView() = default;

    ImageView(
        vk::raii::Device const& device,
        vk::Image image,
        vk::Format format,
        vk::ImageAspectFlags aspect_flags = vk::ImageAspectFlagBits::eColor,
        vk::ImageViewType view_type = vk::ImageViewType::e2D,
        uint32_t base_mip_level = 0,
        uint32_t level_count = 1,
        uint32_t base_array_layer = 0,
        uint32_t layer_count = 1,
        vk::ComponentMapping components = {
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity
        }
    ) {
        handle = device.createImageView(
            vk::ImageViewCreateInfo{}
                .setImage(image)
                .setViewType(view_type)
                .setFormat(format)
                .setComponents(components)
                .setSubresourceRange(vk::ImageSubresourceRange{
                    aspect_flags,
                    base_mip_level,
                    level_count,
                    base_array_layer,
                    layer_count
                })
        );
    }

    ImageView(ImageView&&) noexcept = default;
    ImageView& operator=(ImageView&&) noexcept = default;

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    auto get() const -> const vk::raii::ImageView& { 
        return handle; 
    }

private:
    vk::raii::ImageView handle = nullptr;
};

#endif