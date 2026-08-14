#include "resource/registry/resource_registry.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

auto ResourceRegistry::add(std::unique_ptr<Material> material) -> ResourceId<Material> {
    return materials_.add(std::move(material));
}

auto ResourceRegistry::add(std::unique_ptr<Model> model) -> ResourceId<Model> {
    return models_.add(std::move(model));
}

auto ResourceRegistry::set_model_name(ResourceId<Model> id, std::string name) -> void {
    if (name.empty()) {
        throw std::invalid_argument("model name cannot be empty");
    }
    static_cast<void>(models_.query(id));

    const auto name_entry = model_names_.find(name);
    if (name_entry != model_names_.end()) {
        if (name_entry->second == id) {
            return;
        }
        throw std::invalid_argument(
            "model name is already registered: " + name_entry->first
        );
    }

    std::optional<std::string> previous_name;
    for (const auto& [registered_name, registered_id] : model_names_) {
        if (registered_id == id) {
            previous_name = registered_name;
            break;
        }
    }

    model_names_.emplace(std::move(name), id);
    if (previous_name) {
        model_names_.erase(*previous_name);
    }
}

auto ResourceRegistry::query(ResourceId<Material> id) const -> const Material& {
    return materials_.query(id);
}

auto ResourceRegistry::query(ResourceId<Model> id) const -> const Model& {
    return models_.query(id);
}

auto ResourceRegistry::query_model_id(std::string_view name) const
    -> ResourceId<Model> {
    const auto name_entry = model_names_.find(std::string{name});
    if (name_entry == model_names_.end()) {
        throw std::out_of_range(
            "model name is not registered: " + std::string{name}
        );
    }
    return name_entry->second;
}

auto ResourceRegistry::query_model(std::string_view name) const -> const Model& {
    return query(query_model_id(name));
}

auto ResourceRegistry::contains_model(std::string_view name) const -> bool {
    return model_names_.contains(std::string{name});
}
