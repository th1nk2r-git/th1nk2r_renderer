#ifndef MODEL_HPP
#define MODEL_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "resource/gpu/primitive.hpp"

class Model {
public:
    Model() = delete;
    explicit Model(std::vector<Primitive> primitives);
    ~Model() = default;

    Model(const Model&) = delete;
    auto operator=(const Model&) -> Model& = delete;
    Model(Model&&) noexcept = default;
    auto operator=(Model&&) noexcept -> Model& = default;

    auto primitives() const noexcept -> std::span<const Primitive> {
        return primitives_;
    }

private:
    std::vector<Primitive> primitives_;
};

#endif
