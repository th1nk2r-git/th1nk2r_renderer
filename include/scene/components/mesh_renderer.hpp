#ifndef MESH_RENDERER_HPP
#define MESH_RENDERER_HPP

#include "resource/gpu/resource_id.hpp"
#include "scene/component.hpp"

class Model;

class MeshRenderer final : public Component {
public:
    explicit MeshRenderer(ResourceId<Model> model);

    auto model_id() const noexcept -> ResourceId<Model> {
        return model_id_;
    }

    auto set_model(ResourceId<Model> model) -> void;

private:
    ResourceId<Model> model_id_;
};

#endif
