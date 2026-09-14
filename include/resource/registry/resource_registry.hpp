#ifndef RESOURCE_REGISTRY_HPP
#define RESOURCE_REGISTRY_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gfx/resource/buffer.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/resource_id.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/registry/resource_pool.hpp"

class BufferUploader;

class ResourceRegistry {
public:
    ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry&) = delete;
    auto operator=(const ResourceRegistry&) -> ResourceRegistry& = delete;
    ResourceRegistry(ResourceRegistry&&) = delete;
    auto operator=(ResourceRegistry&&) -> ResourceRegistry& = delete;

    auto add(std::unique_ptr<Texture> texture) -> ResourceId<Texture>;
    auto add(std::unique_ptr<Material> material) -> ResourceId<Material>;
    // Stores CPU geometry and assigns a range without creating GPU buffers.
    auto add(MeshData data) -> ResourceId<Mesh>;
    auto add(std::unique_ptr<Model> model) -> ResourceId<Model>;

    // Call once after importing all meshes, then submit uploads before drawing.
    auto upload_meshes(const MemoryAllocator& allocator, BufferUploader& uploader) -> void;
    auto vertex_buffer() const -> const Buffer&;
    auto index_buffer() const -> const Buffer&;

    auto query(ResourceId<Texture> id) const -> const Texture&;
    auto query(ResourceId<Material> id) const -> const Material&;
    auto query(ResourceId<Mesh> id) const -> const Mesh&;
    auto query(ResourceId<Model> id) const -> const Model&;

    auto set_model_name(ResourceId<Model> id, std::string name) -> void;
    auto query_model_id(std::string_view name) const -> ResourceId<Model>;
    auto query_model(std::string_view name) const -> const Model&;
    auto contains_model(std::string_view name) const -> bool;

private:
    ResourcePool<Texture> textures_;
    ResourcePool<Material> materials_;
    ResourcePool<Mesh> meshes_;
    ResourcePool<Model> models_;
    std::unordered_map<std::string, ResourceId<Model>> model_names_;

    enum class GeometryState {
        Collecting,
        Enqueueing,
        Enqueued
    };

    std::vector<MeshData> pending_mesh_data_;
    uint64_t vertex_count_ = 0;
    uint64_t index_count_ = 0;
    Buffer vertex_buffer_;
    Buffer index_buffer_;
    GeometryState geometry_state_ = GeometryState::Collecting;
};

#endif
