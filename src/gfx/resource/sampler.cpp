#include "gfx/resource/sampler.hpp"

#include <cmath>
#include <stdexcept>

namespace {
    auto validate_sampler_desc(const SamplerDesc& desc) -> void {
        if (!std::isfinite(desc.min_lod) ||
            !std::isfinite(desc.max_lod)) {
            throw std::invalid_argument(
                "sampler LOD bounds must be finite!"
            );
        }

        if (desc.min_lod > desc.max_lod) {
            throw std::invalid_argument(
                "sampler min LOD cannot exceed max LOD!"
            );
        }
    }

    auto create_sampler(
        const Device& device,
        const SamplerDesc& desc
    ) -> vk::raii::Sampler {
        validate_sampler_desc(desc);

        vk::SamplerCreateInfo create_info{};
        create_info
            .setMagFilter(desc.mag_filter)
            .setMinFilter(desc.min_filter)
            .setMipmapMode(desc.mipmap_mode)
            .setAddressModeU(desc.address_u)
            .setAddressModeV(desc.address_v)
            .setAddressModeW(desc.address_w)
            .setMipLodBias(0.0F)
            .setAnisotropyEnable(false)
            .setMaxAnisotropy(1.0F)
            .setCompareEnable(false)
            .setCompareOp(vk::CompareOp::eNever)
            .setMinLod(desc.min_lod)
            .setMaxLod(desc.max_lod)
            .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
            .setUnnormalizedCoordinates(false);

        return device.logical_device().createSampler(create_info);
    }
}

Sampler::Sampler(const Device& device, const SamplerDesc& desc)
    : handle_(create_sampler(device, desc)) {}

auto Sampler::get() const noexcept -> const vk::raii::Sampler& {
    return handle_;
}
