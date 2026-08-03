#ifndef SAMPLER_HPP
#define SAMPLER_HPP

#include "gfx/device/device.hpp"

struct SamplerDesc {
    vk::Filter mag_filter = vk::Filter::eLinear;
    vk::Filter min_filter = vk::Filter::eLinear;
    vk::SamplerMipmapMode mipmap_mode = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode address_u = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode address_v = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode address_w = vk::SamplerAddressMode::eRepeat;

    float min_lod = 0.0F;
    float max_lod = 0.0F;
};

class Sampler {
public:
    Sampler() = default;
    Sampler(const Device& device, const SamplerDesc& desc);

    auto get() const noexcept -> const vk::raii::Sampler&;

private:
    vk::raii::Sampler handle_ = nullptr;
};

#endif