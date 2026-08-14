#ifndef RESOURCE_REGISTRY_HPP
#define RESOURCE_REGISTRY_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "resource/gpu/material.hpp"
#include "resource/gpu/resource_id.hpp"
#include "resource/registry/resource_pool.hpp"
#include "resource/gpu/model.hpp"

class ResourceRegistry {
public:
    ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry&) = delete;
    auto operator=(const ResourceRegistry&) -> ResourceRegistry& = delete;
    ResourceRegistry(ResourceRegistry&&) = delete;
    auto operator=(ResourceRegistry&&) -> ResourceRegistry& = delete;

    auto add(std::unique_ptr<Material> material) -> ResourceId<Material>;
    auto add(std::unique_ptr<Model> model) -> ResourceId<Model>;

    auto query(ResourceId<Material> id) const -> const Material&;
    auto query(ResourceId<Model> id) const -> const Model&;

    auto set_model_name(ResourceId<Model> id, std::string name) -> void;
    auto query_model_id(std::string_view name) const -> ResourceId<Model>;
    auto query_model(std::string_view name) const -> const Model&;
    auto contains_model(std::string_view name) const -> bool;

private:
    ResourcePool<Material> materials_;
    ResourcePool<Model> models_;
    std::unordered_map<std::string, ResourceId<Model>> model_names_;
};

#endif
