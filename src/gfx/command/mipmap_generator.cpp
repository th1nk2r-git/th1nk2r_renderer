#include "gfx/command/mipmap_generator.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace {
    auto mip_subresource_range(
        const MipmapGenerationDesc& desc,
        uint32_t mip_level
    ) -> vk::ImageSubresourceRange {
        return vk::ImageSubresourceRange{
            desc.aspect_mask,
            mip_level,
            1,
            desc.base_array_layer,
            desc.layer_count
        };
    }

    auto validate_desc(const MipmapGenerationDesc& desc) -> void {
        if (!desc.image) {
            throw std::invalid_argument(
                "mipmap generation requires a valid image!"
            );
        }

        if (desc.base_extent.width == 0 ||
            desc.base_extent.height == 0 ||
            desc.base_extent.depth != 1) {
            throw std::invalid_argument(
                "mipmap generation requires a non-empty 2D extent!"
            );
        }

        if (!desc.aspect_mask || desc.layer_count == 0) {
            throw std::invalid_argument(
                "mipmap generation requires a valid subresource range!"
            );
        }

        if (desc.mip_levels < 2) {
            throw std::invalid_argument(
                "mipmap generation requires more than one mip level!"
            );
        }

        if (desc.final_layout == vk::ImageLayout::eUndefined ||
            desc.final_layout == vk::ImageLayout::ePreinitialized ||
            !desc.destination_stage ||
            !desc.destination_access) {
            throw std::invalid_argument(
                "mipmap generation requires a usable destination state!"
            );
        }
    }
}

auto record_mipmap_generation(
    vk::raii::CommandBuffer& command_buffer,
    const MipmapGenerationDesc& desc
) -> void {
    validate_desc(desc);

    auto mip_width = static_cast<int32_t>(desc.base_extent.width);
    auto mip_height = static_cast<int32_t>(desc.base_extent.height);

    for (uint32_t mip_level = 1;
         mip_level < desc.mip_levels;
         ++mip_level) {
        const auto source_mip_level = mip_level - 1;

        vk::ImageMemoryBarrier source_barrier{};
        source_barrier
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(desc.image)
            .setSubresourceRange(
                mip_subresource_range(desc, source_mip_level)
            );

        vk::ImageMemoryBarrier destination_barrier{};
        destination_barrier
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(desc.image)
            .setSubresourceRange(
                mip_subresource_range(desc, mip_level)
            );

        const std::array prepare_barriers{
            source_barrier,
            destination_barrier
        };
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            {},
            {},
            prepare_barriers
        );

        const auto next_width = std::max(mip_width / 2, 1);
        const auto next_height = std::max(mip_height / 2, 1);

        vk::ImageBlit blit{};
        blit.srcSubresource = vk::ImageSubresourceLayers{
            desc.aspect_mask,
            source_mip_level,
            desc.base_array_layer,
            desc.layer_count
        };
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{
            mip_width,
            mip_height,
            1
        };
        blit.dstSubresource = vk::ImageSubresourceLayers{
            desc.aspect_mask,
            mip_level,
            desc.base_array_layer,
            desc.layer_count
        };
        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{
            next_width,
            next_height,
            1
        };

        command_buffer.blitImage(
            desc.image,
            vk::ImageLayout::eTransferSrcOptimal,
            desc.image,
            vk::ImageLayout::eTransferDstOptimal,
            blit,
            desc.filter
        );

        vk::ImageMemoryBarrier source_final_barrier{};
        source_final_barrier
            .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
            .setDstAccessMask(desc.destination_access)
            .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setNewLayout(desc.final_layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(desc.image)
            .setSubresourceRange(
                mip_subresource_range(desc, source_mip_level)
            );

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            desc.destination_stage,
            {},
            {},
            {},
            source_final_barrier
        );

        mip_width = next_width;
        mip_height = next_height;
    }

    vk::ImageMemoryBarrier last_mip_barrier{};
    last_mip_barrier
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(desc.destination_access)
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(desc.final_layout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(desc.image)
        .setSubresourceRange(
            mip_subresource_range(desc, desc.mip_levels - 1)
        );

    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        desc.destination_stage,
        {},
        {},
        {},
        last_mip_barrier
    );
}
