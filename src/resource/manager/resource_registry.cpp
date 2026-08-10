#include "resource/manager/resource_registry.hpp"

#include <stdexcept>
#include <utility>

auto ResourceRegistry::add(std::unique_ptr<Material> material) -> ResourceId<Material> {
    return materials_.add(std::move(material));
}

auto ResourceRegistry::add_model(
    std::string name,
    std::unique_ptr<Model> model
) -> ResourceId<Model> {
    if (name.empty()) {
        throw std::invalid_argument("model name cannot be empty");
    }

    auto [name_entry, inserted] = model_names_.try_emplace(
        std::move(name)
    );
    if (!inserted) {
        throw std::invalid_argument(
            "model name is already registered: " + name_entry->first
        );
    }

    try {
        const auto id = models_.add(std::move(model));
        name_entry->second = id;
        return id;
    }
    catch (...) {
        model_names_.erase(name_entry);
        throw;
    }
}

auto ResourceRegistry::query(ResourceId<Material> id) const -> const Material& {
    return materials_.query(id);
}

auto ResourceRegistry::query(ResourceId<Model> id) const -> const Model& {
    return models_.query(id);
}

auto ResourceRegistry::query_model(std::string_view name) const -> const Model& {
    const auto name_entry = model_names_.find(std::string{name});
    if (name_entry == model_names_.end()) {
        throw std::out_of_range(
            "model name is not registered: " + std::string{name}
        );
    }
    return models_.query(name_entry->second);
}

auto ResourceRegistry::contains_model(std::string_view name) const -> bool {
    return model_names_.contains(std::string{name});
}
