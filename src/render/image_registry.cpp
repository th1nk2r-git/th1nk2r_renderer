#include "render/image_registry.hpp"

#include <stdexcept>
#include <utility>

auto ImageRegistry::add(std::string name, Image image) -> void {
    if (name.empty()) {
        throw std::invalid_argument("image name cannot be empty!");
    }
    if (!image.get()) {
        throw std::invalid_argument("cannot register an invalid image!");
    }

    if (external_images_.contains(name)) {
        throw std::invalid_argument(
            "an external image named '" + name + "' is already registered!"
        );
    }

    const bool inserted = images_.try_emplace(
        name,
        std::move(image)
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "an image named '" + name + "' is already registered!"
        );
    }
}

auto ImageRegistry::bind_external(std::string name, Image& image) -> void {
    if (name.empty()) {
        throw std::invalid_argument("image name cannot be empty!");
    }
    if (!image.get()) {
        throw std::invalid_argument("cannot register an invalid image!");
    }
    if (images_.contains(name)) {
        throw std::invalid_argument(
            "an owned image named '" + name + "' is already registered!"
        );
    }

    external_images_.insert_or_assign(std::move(name), &image);
}

auto ImageRegistry::query(std::string_view name) -> Image& {
    const std::string resource_name{name};
    if (const auto iterator = images_.find(resource_name);
        iterator != images_.end()) {
        return iterator->second;
    }
    if (const auto iterator = external_images_.find(resource_name);
        iterator != external_images_.end()) {
        return *iterator->second;
    }
    throw std::out_of_range(
        "no image named '" + resource_name + "' is registered!"
    );
}

auto ImageRegistry::query(std::string_view name) const -> const Image& {
    const std::string resource_name{name};
    if (const auto iterator = images_.find(resource_name);
        iterator != images_.end()) {
        return iterator->second;
    }
    if (const auto iterator = external_images_.find(resource_name);
        iterator != external_images_.end()) {
        return *iterator->second;
    }
    throw std::out_of_range(
        "no image named '" + resource_name + "' is registered!"
    );
}
