#ifndef IMAGE_VIEW_HPP
#define IMAGE_VIEW_HPP

#include "gfx/vulkan/device.hpp"

class ImageView {
public:
    ImageView() = default;

    ImageView(
        const Device& device,
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
        handle_ = device.logical_device().createImageView(
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

    ~ImageView() = default;

    // return the const reference of the image view
    auto get() const -> const vk::raii::ImageView& { 
        return handle_; 
    }

private:
    vk::raii::ImageView handle_ = nullptr;
};

#endif