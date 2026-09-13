#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "scene/component.hpp"

class Entity {
public:
    Entity() = default;
    ~Entity() = default;

    Entity(const Entity&) = delete;
    auto operator=(const Entity&) -> Entity& = delete;
    Entity(Entity&&) noexcept = default;
    auto operator=(Entity&&) noexcept -> Entity& = default;

    template <typename T, typename... Args> requires std::derived_from<T, Component>
    auto add_component(Args&&... args) -> T& {
        if (has_component<T>()) {
            throw std::logic_error(
                "entity already contains the requested component type"
            );
        }

        auto component = std::make_unique<T>(
            std::forward<Args>(args)...
        );
        auto& result = *component;
        components_.push_back(std::move(component));
        return result;
    }

    template <typename T> requires std::derived_from<T, Component>
    auto get_component() noexcept -> T* {
        for (const auto& component : components_) {
            if (auto* result = dynamic_cast<T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    template <typename T> requires std::derived_from<T, Component>
    auto get_component() const noexcept -> const T* {
        for (const auto& component : components_) {
            if (const auto* result = dynamic_cast<const T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    template <typename T> requires std::derived_from<T, Component>
    auto has_component() const noexcept -> bool {
        return get_component<T>() != nullptr;
    }

    template <typename T> requires std::derived_from<T, Component>
    auto remove_component() noexcept -> bool {
        const auto iterator = std::find_if(
            components_.begin(),
            components_.end(),
            [](const auto& component) {
                return dynamic_cast<T*>(component.get()) != nullptr;
            }
        );
        if (iterator == components_.end()) {
            return false;
        }

        components_.erase(iterator);
        return true;
    }

    auto component_count() const noexcept -> std::size_t {
        return components_.size();
    }

private:
    std::vector<std::unique_ptr<Component>> components_;
};

#endif
