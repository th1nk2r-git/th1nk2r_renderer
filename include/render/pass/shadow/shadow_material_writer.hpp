#ifndef SHADOW_MATERIAL_WRITER_HPP
#define SHADOW_MATERIAL_WRITER_HPP

#include <cstdint>
#include <unordered_map>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "resource/gpu/resource_id.hpp"

class Material;

class ShadowMaterialWriter {
public:
    ShadowMaterialWriter(
        const Device& device,
        const MemoryAllocator& allocator,
        uint32_t max_material_count = 1024
    );

    ShadowMaterialWriter(const ShadowMaterialWriter&) = delete;
    auto operator=(const ShadowMaterialWriter&) -> ShadowMaterialWriter& = delete;
    ShadowMaterialWriter(ShadowMaterialWriter&&) = delete;
    auto operator=(ShadowMaterialWriter&&) -> ShadowMaterialWriter& = delete;

    auto write(ResourceId<Material> id, const Material& material) -> void;

    auto descriptor_set_layout() const noexcept -> const vk::raii::DescriptorSetLayout& {
        return descriptor_set_layout_;
    }

    auto descriptor_set(ResourceId<Material> id) const
        -> const vk::raii::DescriptorSet& {
        return bindings_.at(id.value()).descriptor_set;
    }

private:
    struct MaterialBinding {
        vk::raii::Sampler sampler = nullptr;
        vk::raii::DescriptorSet descriptor_set = nullptr;
    };

    const Device& device_;
    uint32_t max_material_count_ = 0;
    uint32_t next_material_index_ = 0;
    vk::DeviceSize parameter_stride_ = 0;
    vk::raii::DescriptorSetLayout descriptor_set_layout_ = nullptr;
    Buffer parameter_buffer_;
    vk::raii::DescriptorPool descriptor_pool_ = nullptr;
    std::unordered_map<uint32_t, MaterialBinding> bindings_;
};

#endif
