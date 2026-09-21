#include "render/image_registry.hpp"

#include <stdexcept>
#include <utility>

auto ImageRegistry::add(
    std::string name,
    std::vector<Image> images
) -> void {
    if (name.empty()) {
        throw std::invalid_argument("image name cannot be empty!");
    }
    if (images.empty()) {
        throw std::invalid_argument(
            "cannot register an empty image instance set!"
        );
    }
    for (const auto& image : images) {
        if (!image.get()) {
            throw std::invalid_argument("cannot register an invalid image!");
        }
    }

    if (external_images_.contains(name)) {
        throw std::invalid_argument(
            "an external image named '" + name + "' is already registered!"
        );
    }

    const bool inserted = images_.try_emplace(
        name,
        std::move(images)
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
    return query(name, 0);
}

auto ImageRegistry::query(std::string_view name) const -> const Image& {
    return query(name, 0);
}

auto ImageRegistry::query(
    std::string_view name,
    uint32_t instance
) -> Image& {
    const std::string resource_name{name};
    if (const auto iterator = images_.find(resource_name);
        iterator != images_.end()) {
        return iterator->second.at(instance);
    }
    if (const auto iterator = external_images_.find(resource_name);
        iterator != external_images_.end()) {
        if (instance != 0) {
            throw std::out_of_range(
                "external image '" + resource_name +
                "' only has one bound instance!"
            );
        }
        return *iterator->second;
    }
    throw std::out_of_range(
        "no image named '" + resource_name + "' is registered!"
    );
}

auto ImageRegistry::query(
    std::string_view name,
    uint32_t instance
) const -> const Image& {
    const std::string resource_name{name};
    if (const auto iterator = images_.find(resource_name);
        iterator != images_.end()) {
        return iterator->second.at(instance);
    }
    if (const auto iterator = external_images_.find(resource_name);
        iterator != external_images_.end()) {
        if (instance != 0) {
            throw std::out_of_range(
                "external image '" + resource_name +
                "' only has one bound instance!"
            );
        }
        return *iterator->second;
    }
    throw std::out_of_range(
        "no image named '" + resource_name + "' is registered!"
    );
}

auto ImageRegistry::contains(std::string_view name) const -> bool {
    const std::string resource_name{name};
    return images_.contains(resource_name) ||
        external_images_.contains(resource_name);
}

auto ImageRegistry::instance_count(std::string_view name) const -> uint32_t {
    const std::string resource_name{name};
    if (const auto iterator = images_.find(resource_name);
        iterator != images_.end()) {
        return static_cast<uint32_t>(iterator->second.size());
    }
    if (external_images_.contains(resource_name)) {
        return 1;
    }
    throw std::out_of_range(
        "no image named '" + resource_name + "' is registered!"
    );
}

auto ImageRegistry::clear() -> void {
    images_.clear();
    external_images_.clear();
}
