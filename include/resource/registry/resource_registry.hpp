#ifndef RESOURCE_REGISTRY_HPP
#define RESOURCE_REGISTRY_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/resource_id.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/registry/resource_pool.hpp"

class ResourceRegistry {
public:
    ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry&) = delete;
    auto operator=(const ResourceRegistry&) -> ResourceRegistry& = delete;
    ResourceRegistry(ResourceRegistry&&) = delete;
    auto operator=(ResourceRegistry&&) -> ResourceRegistry& = delete;

    auto add(std::unique_ptr<Texture> texture) -> ResourceId<Texture>;
    auto add(std::unique_ptr<Material> material) -> ResourceId<Material>;
    auto add(std::unique_ptr<Mesh> mesh) -> ResourceId<Mesh>;
    auto add(std::unique_ptr<Model> model) -> ResourceId<Model>;

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
};

#endif
