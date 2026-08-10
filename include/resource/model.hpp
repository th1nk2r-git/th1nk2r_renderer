#ifndef MODEL_HPP
#define MODEL_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "resource/mesh.hpp"

class Model {
public:
    Model() = delete;
    explicit Model(std::vector<Mesh> meshes);
    ~Model() = default;

    Model(const Model&) = delete;
    auto operator=(const Model&) -> Model& = delete;
    Model(Model&&) noexcept = default;
    auto operator=(Model&&) noexcept -> Model& = default;

    auto meshes() const noexcept -> std::span<const Mesh> {
        return std::span<const Mesh>{meshes_};
    }

private:
    std::vector<Mesh> meshes_;
};

#endif
