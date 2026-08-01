#ifndef RENDER_MESH_HPP
#define RENDER_MESH_HPP

#include "gfx/device/gpu_allocator.hpp"
#include "gfx/resources/buffer.hpp"
#include "gfx/resources/data_uploader.hpp"
#include "geometry/mesh.hpp"

class RenderMesh {
public:
    RenderMesh() = default;
    RenderMesh(const Mesh& data, const GpuAllocator& allocator, DataUploader& uploader);

    RenderMesh(const RenderMesh&) = delete;
    auto operator=(const RenderMesh&) -> RenderMesh& = delete;

    RenderMesh(RenderMesh&& other) noexcept = default;
    auto operator=(RenderMesh&& other) noexcept -> RenderMesh& = default;

    auto bind(vk::raii::CommandBuffer& command_buffer) const -> void;
    auto index_count() const noexcept -> uint32_t {
        return index_count_;
    }

private:
    uint32_t index_count_;
    Buffer vertex_buffer_;
    Buffer index_buffer_;
};

#endif