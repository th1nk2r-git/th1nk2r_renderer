#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <filesystem>
#include <string>
#include <string_view>

#include "gfx/descriptor/descriptor_pool.hpp"
#include "resource/manager/model_importer.hpp"
#include "resource/manager/resource_registry.hpp"

class ResourceManager {
public:
    ResourceManager() = delete;
    explicit ResourceManager(ImportContext context);

    ResourceManager(const ResourceManager&) = delete;
    auto operator=(const ResourceManager&) -> ResourceManager& = delete;
    ResourceManager(ResourceManager&&) = delete;
    auto operator=(ResourceManager&&) -> ResourceManager& = delete;

    auto import_model(
        const std::filesystem::path& path,
        std::string name
    ) -> ResourceId<Model>;

    auto query(ResourceId<Material> id) const -> const Material&;
    auto query(ResourceId<Model> id) const -> const Model&;
    auto query_model(std::string_view name) const -> const Model&;
    auto contains_model(std::string_view name) const -> bool;

    auto registry() const noexcept -> const ResourceRegistry& {
        return registry_;
    }

private:
    DescriptorPool material_descriptor_pool_;
    ResourceRegistry registry_;
    ModelImporter importer_;
};

#endif
