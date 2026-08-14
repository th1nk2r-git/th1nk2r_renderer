#ifndef CAMERA_WRITER_HPP
#define CAMERA_WRITER_HPP

#include <cstdint>
#include <vector>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "render/pass/forward/view_projection.hpp"

class CameraWriter {
public:
    CameraWriter(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout,
        uint32_t frame_count
    );

    CameraWriter(const CameraWriter&) = delete;
    auto operator=(const CameraWriter&) -> CameraWriter& = delete;
    CameraWriter(CameraWriter&&) = delete;
    auto operator=(CameraWriter&&) -> CameraWriter& = delete;

    auto write(uint32_t frame_index, const ViewProjection& uniforms) -> void;

    auto descriptor_set(uint32_t frame_index) const
        -> const vk::raii::DescriptorSet& {
        return descriptor_sets_.at(frame_index);
    }

private:
    vk::raii::DescriptorPool descriptor_pool_ = nullptr;
    std::vector<Buffer> uniform_buffers_;
    std::vector<vk::raii::DescriptorSet> descriptor_sets_;
};

#endif
