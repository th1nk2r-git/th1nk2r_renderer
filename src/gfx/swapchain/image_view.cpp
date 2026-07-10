#include "gfx/swapchain/image_view.hpp"

ImageView::ImageView(
    const Device& device,
    vk::Image image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags,
    vk::ImageViewType view_type,
    uint32_t base_mip_level,
    uint32_t level_count,
    uint32_t base_array_layer,
    uint32_t layer_count,
    vk::ComponentMapping components
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
                layer_count}));
}
