#ifndef ASSETS_POOL_HPP
#define ASSETS_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "resource/gpu/resource_id.hpp"

template <typename T>
class AssetsPool {
public:
    AssetsPool() = default;

    AssetsPool(const AssetsPool&) = delete;
    auto operator=(const AssetsPool&) -> AssetsPool& = delete;
    AssetsPool(AssetsPool&&) noexcept = default;
    auto operator=(AssetsPool&&) noexcept -> AssetsPool& = default;

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

    auto size() const noexcept -> std::size_t {
        return resources_.size();
    }

private:
    std::vector<std::unique_ptr<T>> resources_;
};

#endif
