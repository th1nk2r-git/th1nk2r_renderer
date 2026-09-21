#include "render/buffer_registry.hpp"

#include <stdexcept>
#include <utility>

auto BufferRegistry::add(
    std::string name,
    std::vector<Buffer> buffers
) -> void {
    if (name.empty()) {
        throw std::invalid_argument("buffer name cannot be empty!");
    }
    if (buffers.empty()) {
        throw std::invalid_argument(
            "cannot register an empty buffer instance set!"
        );
    }
    for (const auto& buffer : buffers) {
        if (!buffer.get()) {
            throw std::invalid_argument("cannot register an invalid buffer!");
        }
    }

    if (external_buffers_.contains(name)) {
        throw std::invalid_argument(
            "an external buffer named '" + name + "' is already registered!"
        );
    }

    const bool inserted = buffers_.try_emplace(
        name,
        std::move(buffers)
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "a buffer named '" + name + "' is already registered!"
        );
    }
}

auto BufferRegistry::bind_external(std::string name, Buffer& buffer) -> void {
    if (name.empty()) {
        throw std::invalid_argument("buffer name cannot be empty!");
    }
    if (!buffer.get()) {
        throw std::invalid_argument("cannot register an invalid buffer!");
    }
    if (buffers_.contains(name)) {
        throw std::invalid_argument(
            "an owned buffer named '" + name + "' is already registered!"
        );
    }

    external_buffers_.insert_or_assign(std::move(name), &buffer);
}

auto BufferRegistry::query(std::string_view name) -> Buffer& {
    return query(name, 0);
}

auto BufferRegistry::query(std::string_view name) const -> const Buffer& {
    return query(name, 0);
}

auto BufferRegistry::query(
    std::string_view name,
    uint32_t instance
) -> Buffer& {
    const std::string resource_name{name};
    if (const auto iterator = buffers_.find(resource_name);
        iterator != buffers_.end()) {
        return iterator->second.at(instance);
    }
    if (const auto iterator = external_buffers_.find(resource_name);
        iterator != external_buffers_.end()) {
        if (instance != 0) {
            throw std::out_of_range(
                "external buffer '" + resource_name +
                "' only has one bound instance!"
            );
        }
        return *iterator->second;
    }
    throw std::out_of_range(
        "no buffer named '" + resource_name + "' is registered!"
    );
}

auto BufferRegistry::query(
    std::string_view name,
    uint32_t instance
) const -> const Buffer& {
    const std::string resource_name{name};
    if (const auto iterator = buffers_.find(resource_name);
        iterator != buffers_.end()) {
        return iterator->second.at(instance);
    }
    if (const auto iterator = external_buffers_.find(resource_name);
        iterator != external_buffers_.end()) {
        if (instance != 0) {
            throw std::out_of_range(
                "external buffer '" + resource_name +
                "' only has one bound instance!"
            );
        }
        return *iterator->second;
    }
    throw std::out_of_range(
        "no buffer named '" + resource_name + "' is registered!"
    );
}

auto BufferRegistry::contains(std::string_view name) const -> bool {
    const std::string resource_name{name};
    return buffers_.contains(resource_name) ||
        external_buffers_.contains(resource_name);
}

auto BufferRegistry::instance_count(std::string_view name) const -> uint32_t {
    const std::string resource_name{name};
    if (const auto iterator = buffers_.find(resource_name);
        iterator != buffers_.end()) {
        return static_cast<uint32_t>(iterator->second.size());
    }
    if (external_buffers_.contains(resource_name)) {
        return 1;
    }
    throw std::out_of_range(
        "no buffer named '" + resource_name + "' is registered!"
    );
}

auto BufferRegistry::clear() -> void {
    buffers_.clear();
    external_buffers_.clear();
}
