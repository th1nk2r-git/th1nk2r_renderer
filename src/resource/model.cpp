#include "resource/gpu/model.hpp"

#include <stdexcept>
#include <utility>

Model::Model(std::vector<Primitive> primitives)
    : primitives_(std::move(primitives)) {
    if (primitives_.empty()) {
        throw std::invalid_argument(
            "model requires at least one primitive"
        );
    }

    for (const auto& primitive : primitives_) {
        if (!primitive.mesh.valid() || !primitive.material.valid()) {
            throw std::invalid_argument(
                "model primitive contains an invalid resource id"
            );
        }
    }
}
