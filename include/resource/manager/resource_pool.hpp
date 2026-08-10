#ifndef RESOURCE_POOL_HPP
#define RESOURCE_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "resource/manager/resource_id.hpp"

template <typename T>
class ResourcePool {
public:
    ResourcePool() = default;

    ResourcePool(const ResourcePool&) = delete;
    auto operator=(const ResourcePool&) -> ResourcePool& = delete;
    ResourcePool(ResourcePool&&) noexcept = default;
    auto operator=(ResourcePool&&) noexcept -> ResourcePool& = default;

    auto add(std::unique_ptr<T> resource) -> ResourceId<T> {
        if (resource == nullptr) {
            throw std::invalid_argument("resource cannot be null");
        }
        if (resources_.size() >= ResourceId<T>::invalid_value) {
            throw std::length_error("resource pool has exhausted its ids");
        }
        resources_.push_back(std::move(resource));
        return ResourceId<T>{
            static_cast<uint32_t>(resources_.size() - 1)
        };
    }

    auto query(ResourceId<T> id) const -> const T& {
        if (!id.valid()) {
            throw std::out_of_range("resource id is invalid");
        }
        const auto index = static_cast<size_t>(id.value());
        if (index >= resources_.size()) {
            throw std::out_of_range("resource id is out of range");
        }
        return *resources_[index];
    }

private:
    std::vector<std::unique_ptr<T>> resources_;
};

#endif
