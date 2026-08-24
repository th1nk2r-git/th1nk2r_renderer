#ifndef LIGHT_WRITER_HPP
#define LIGHT_WRITER_HPP

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "scene/light.hpp"

struct PointLightShadowBinding;

class LightWriter {
public:
    LightWriter(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout,
        uint32_t frame_count,
        uint32_t max_light_count = 1024
    );

    LightWriter(const LightWriter&) = delete;
    auto operator=(const LightWriter&) -> LightWriter& = delete;
    LightWriter(LightWriter&&) = delete;
    auto operator=(LightWriter&&) -> LightWriter& = delete;

    auto write(
        uint32_t frame_index,
        const std::vector<PointLight>& lights,
        std::span<const PointLightShadowBinding> shadow_bindings
    ) -> void;

    auto descriptor_set(uint32_t frame_index) const
        -> const vk::raii::DescriptorSet& {
        return descriptor_sets_.at(frame_index);
    }

    auto light_count(uint32_t frame_index) const -> uint32_t {
        return light_counts_.at(frame_index);
    }

private:
    uint32_t max_light_count_ = 0;
    vk::DeviceSize frame_stride_ = 0;
    Buffer light_buffer_;
    vk::raii::DescriptorPool descriptor_pool_ = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets_;
    std::vector<uint32_t> light_counts_;
    std::vector<glm::vec4> staging_data_;
};

#endif
