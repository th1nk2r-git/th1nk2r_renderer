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

auto ImageRegistry::image(std::string_view name) -> Image& {
    const auto iterator = images_.find(std::string{name});
    if (iterator == images_.end()) {
        throw std::out_of_range(
            "no image named '" + std::string{name} + "' is registered!"
        );
    }
    return iterator->second;
}

auto ImageRegistry::image(std::string_view name) const -> const Image& {
    const auto iterator = images_.find(std::string{name});
    if (iterator == images_.end()) {
        throw std::out_of_range(
            "no image named '" + std::string{name} + "' is registered!"
        );
    }
    return iterator->second;
}
