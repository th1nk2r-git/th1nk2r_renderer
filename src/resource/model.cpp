#include "resource/gpu/model.hpp"

#include <stdexcept>
#include <utility>

Model::Model(std::vector<Mesh> meshes) : meshes_(std::move(meshes)) {
    if (meshes_.empty()) {
        throw std::invalid_argument(
            "model requires at least one mesh"
        );
    }
}
