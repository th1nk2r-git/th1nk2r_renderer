#include "scene/components/mesh_renderer.hpp"

#include <stdexcept>

namespace {
    auto validate_model(ResourceId<Model> model) -> ResourceId<Model> {
        if (!model.valid()) {
            throw std::invalid_argument(
                "mesh renderer model resource id is invalid"
            );
        }
        return model;
    }
}

MeshRenderer::MeshRenderer(ResourceId<Model> model)
    : model_id_(validate_model(model)) {}

auto MeshRenderer::set_model(ResourceId<Model> model) -> void {
    model_id_ = validate_model(model);
}
