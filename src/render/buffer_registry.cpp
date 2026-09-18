#include "render/buffer_registry.hpp"

#include <stdexcept>
#include <utility>

auto BufferRegistry::add(std::string name, Buffer buffer) -> void {
    if (name.empty()) {
        throw std::invalid_argument("buffer name cannot be empty!");
    }
    if (!buffer.get()) {
        throw std::invalid_argument("cannot register an invalid buffer!");
    }

    const bool inserted = buffers_.try_emplace(
        name,
        std::move(buffer)
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "a buffer named '" + name + "' is already registered!"
        );
    }
}

auto BufferRegistry::buffer(std::string_view name) -> Buffer& {
    const auto iterator = buffers_.find(std::string{name});
    if (iterator == buffers_.end()) {
        throw std::out_of_range(
            "no buffer named '" + std::string{name} + "' is registered!"
        );
    }
    return iterator->second;
}

auto BufferRegistry::buffer(std::string_view name) const -> const Buffer& {
    const auto iterator = buffers_.find(std::string{name});
    if (iterator == buffers_.end()) {
        throw std::out_of_range(
            "no buffer named '" + std::string{name} + "' is registered!"
        );
    }
    return iterator->second;
}
