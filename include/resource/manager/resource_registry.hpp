#ifndef RESOURCE_REGISTRY_HPP
#define RESOURCE_REGISTRY_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "resource/material.hpp"
#include "resource/manager/resource_id.hpp"
#include "resource/manager/resource_pool.hpp"
#include "resource/model.hpp"

class ResourceRegistry {
public:
    ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry&) = delete;
    auto operator=(const ResourceRegistry&) -> ResourceRegistry& = delete;
    ResourceRegistry(ResourceRegistry&&) = delete;
    auto operator=(ResourceRegistry&&) -> ResourceRegistry& = delete;

    auto add(std::unique_ptr<Material> material) -> ResourceId<Material>;
    auto add_model(
        std::string name,
        std::unique_ptr<Model> model
    ) -> ResourceId<Model>;

    auto query(ResourceId<Material> id) const -> const Material&;
    auto query(ResourceId<Model> id) const -> const Model&;
    auto query_model(std::string_view name) const -> const Model&;

    auto contains_model(std::string_view name) const -> bool;

private:
    ResourcePool<Material> materials_;
    ResourcePool<Model> models_;
    std::unordered_map<std::string, ResourceId<Model>> model_names_;
};

#endif
