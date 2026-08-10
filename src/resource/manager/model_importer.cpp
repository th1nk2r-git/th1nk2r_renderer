#include "resource/manager/model_importer.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gfx/descriptor/descriptor_pool.hpp"
#include "resource/manager/resource_registry.hpp"
#include "resource/material.hpp"
#include "resource/mesh.hpp"
#include "resource/model.hpp"
#include "utils/io/model_loader.hpp"

ModelImporter::ModelImporter(ImportContext context) noexcept
    : context_(context) {}

auto ModelImporter::import(
    const std::filesystem::path& path,
    std::string name,
    ResourceRegistry& registry,
    DescriptorPool& material_descriptor_pool
) -> ResourceId<Model> {
    if (name.empty()) {
        throw std::invalid_argument("model name cannot be empty");
    }
    if (registry.contains_model(name)) {
        throw std::invalid_argument(
            "model name is already registered: " + name
        );
    }

    const auto data = load_model(path);
    if (data.material_.empty()) {
        throw std::runtime_error(
            "loaded model does not contain a local material list"
        );
    }

    std::vector<std::optional<ResourceId<Material>>> material_ids(
        data.material_.size()
    );
    std::vector<Mesh> meshes;
    meshes.reserve(data.meshes_.size());

    for (const auto& mesh_data : data.meshes_) {
        const auto material_index = mesh_data.material_index_;
        if (material_index >= data.material_.size()) {
            throw std::out_of_range(
                "mesh local material index is out of range"
            );
        }

        auto& material_id = material_ids[material_index];
        if (!material_id) {
            auto material = std::make_unique<Material>(
                data.material_[material_index],
                context_.device,
                context_.allocator,
                context_.uploader,
                material_descriptor_pool,
                context_.material_descriptor_set_layout
            );
            material_id = registry.add(std::move(material));
        }

        meshes.emplace_back(
            mesh_data,
            *material_id,
            context_.allocator,
            context_.uploader
        );
    }

    auto model = std::make_unique<Model>(std::move(meshes));
    return registry.add_model(
        std::move(name),
        std::move(model)
    );
}
