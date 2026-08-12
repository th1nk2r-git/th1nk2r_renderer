#include "resource/material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
    auto create_sampler_desc(const Texture& texture) -> SamplerDesc {
        SamplerDesc desc{};
        desc.max_lod = static_cast<float>(
            texture.image().mip_levels() - 1
        );
        return desc;
    }

    auto color_byte(float value) noexcept -> std::byte {
        if (!std::isfinite(value)) {
            value = 0.0F;
        }
        const auto scaled = std::clamp(value, 0.0F, 1.0F) * 255.0F;
        return static_cast<std::byte>(
            static_cast<uint8_t>(std::lround(scaled))
        );
    }

    auto create_base_color_texture(
        const MaterialData& data,
        const Device& device,
        const GpuAllocator& allocator,
        DataUploader& uploader
    ) -> std::unique_ptr<Texture> {
        if (data.base_color_texture_) {
            const auto& image = *data.base_color_texture_;
            if (image.channels != 4) {
                throw std::invalid_argument(
                    "material base color texture must contain RGBA8 pixels"
                );
            }
            return std::make_unique<Texture>(
                device,
                allocator,
                uploader,
                image.width,
                image.height,
                image.pixels,
                vk::Format::eR8G8B8A8Srgb
            );
        }

        const std::array pixels{
            color_byte(data.base_color_[0]),
            color_byte(data.base_color_[1]),
            color_byte(data.base_color_[2]),
            color_byte(data.base_color_[3])
        };
        return std::make_unique<Texture>(
            device,
            allocator,
            uploader,
            1,
            1,
            pixels,
            vk::Format::eR8G8B8A8Srgb
        );
    }

    auto allocate_descriptor_set(
        const Device& device,
        DescriptorPool& descriptor_pool,
        const DescriptorSetLayout& descriptor_set_layout
    ) -> vk::raii::DescriptorSet {
        const std::array layouts{
            *descriptor_set_layout.get()
        };
        auto descriptor_sets = descriptor_pool.allocate_sets(
            device,
            layouts
        );

        if (descriptor_sets.size() != 1) {
            throw std::runtime_error(
                "material descriptor allocation returned an unexpected count"
            );
        }

        return std::move(descriptor_sets.front());
    }

    auto write_descriptors(
        const Device& device,
        const vk::raii::DescriptorSet& descriptor_set,
        const Texture& base_color_texture,
        const Sampler& sampler
    ) -> void {
        vk::DescriptorImageInfo texture_info{};
        texture_info
            .setImageView(*base_color_texture.image_view().get())
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        vk::DescriptorImageInfo sampler_info{};
        sampler_info.setSampler(*sampler.get());

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0]
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setImageInfo(sampler_info);
        writes[1]
            .setDstSet(*descriptor_set)
            .setDstBinding(1)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setImageInfo(texture_info);

        device.logical_device().updateDescriptorSets(writes, {});
    }
}

Material::Material(
    const MaterialData& data,
    const Device& device,
    const GpuAllocator& allocator,
    DataUploader& uploader,
    DescriptorPool& descriptor_pool,
    const DescriptorSetLayout& descriptor_set_layout
) : base_color_texture_(
        create_base_color_texture(
            data,
            device,
            allocator,
            uploader
        )
    ),
    sampler_(device, create_sampler_desc(*base_color_texture_)),
    descriptor_set_(
        allocate_descriptor_set(
            device,
            descriptor_pool,
            descriptor_set_layout
        )
    ) {
    write_descriptors(
        device,
        descriptor_set_,
        *base_color_texture_,
        sampler_
    );
}
