#include "gfx/resource/image_view.hpp"

namespace {
    auto create_image_view(const Device& device, const ImageViewDesc& desc) -> vk::raii::ImageView {
        return device.logical_device().createImageView(
            vk::ImageViewCreateInfo{}
                .setImage(desc.image)
                .setViewType(desc.view_type)
                .setFormat(desc.format)
                .setComponents(desc.components)
                .setSubresourceRange(vk::ImageSubresourceRange{
                    desc.aspect_flags,
                    desc.base_mip_level,
                    desc.level_count,
                    desc.base_array_layer,
                    desc.layer_count
                }
            )
        );
    }
}

ImageView::ImageView(const Device& device, const ImageViewDesc& desc)
    : handle_(create_image_view(device, desc)) {}
