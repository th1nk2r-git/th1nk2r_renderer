#ifndef MESH_HPP
#define MESH_HPP

#include "gfx/device/gpu_allocator.hpp"
#include "gfx/device/data_uploader.hpp"
#include "gfx/resource/buffer.hpp"
#include "resource/mesh_data.hpp"

class Mesh {
public:
    Mesh(const MeshData& data, const GpuAllocator& allocator, DataUploader& uploader);

    Mesh(const Mesh&) = delete;
    auto operator=(const Mesh&) -> Mesh& = delete;
    Mesh(Mesh&&) noexcept = default;
    auto operator=(Mesh&&) noexcept -> Mesh& = default;

    auto bind(vk::raii::CommandBuffer& command_buffer) const -> void;
    auto index_count() const noexcept -> uint32_t {
        return index_count_;
    }
    auto material_index() const noexcept -> uint32_t {
        return material_index_;
    }

private:
    uint32_t index_count_ = 0;
    uint32_t material_index_ = 0;
    Buffer vertex_buffer_;
    Buffer index_buffer_;
};

#endif
