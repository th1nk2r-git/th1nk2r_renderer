#ifndef MESH_HPP
#define MESH_HPP

#include "gfx/device/memory_allocator.hpp"
#include "gfx/device/buffer_uploader.hpp"
#include "gfx/resource/buffer.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/resource_id.hpp"
#include <cstdint>

class Material;

class Mesh {
public:
    Mesh(
        const MeshData& data,
        ResourceId<Material> material,
        const MemoryAllocator& allocator,
        BufferUploader& uploader
    );

    Mesh(const Mesh&) = delete;
    auto operator=(const Mesh&) -> Mesh& = delete;
    Mesh(Mesh&&) noexcept = default;
    auto operator=(Mesh&&) noexcept -> Mesh& = default;

    auto bind(vk::raii::CommandBuffer& command_buffer) const -> void;
    auto index_count() const noexcept -> uint32_t {
        return index_count_;
    }
    auto material() const noexcept -> ResourceId<Material> {
        return material_;
    }

    auto set_material(ResourceId<Material> new_material) noexcept -> void {
        material_ = new_material;
    }

private:
    uint32_t index_count_ = 0;
    ResourceId<Material> material_;
    Buffer vertex_buffer_;
    Buffer index_buffer_;
};

#endif
