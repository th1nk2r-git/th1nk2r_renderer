#ifndef SCENE_HPP
#define SCENE_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "scene/camera.hpp"
#include "scene/entity.hpp"
#include "scene/light.hpp"
#include "resource/gpu/resource_id.hpp"


class Scene {
public:
    Scene() = default;

    Scene(const Scene&) = delete;
    auto operator=(const Scene&) -> Scene& = delete;
    Scene(Scene&&) noexcept = default;
    auto operator=(Scene&&) noexcept -> Scene& = default;

    auto camera() noexcept -> Camera& {
        return camera_;
    }

    auto camera() const noexcept -> const Camera& {
        return camera_;
    }

    auto set_camera(Camera&& camera) noexcept -> void;

    auto add_point_light(PointLight point_light) -> void {
        point_lights_.push_back(point_light);
    }

    auto point_lights() const noexcept -> const std::vector<PointLight>& {
        return point_lights_;
    }

    auto entities() noexcept -> std::span<Entity> {
        return entities_;
    }

    auto entities() const noexcept -> std::span<const Entity> {
        return entities_;
    }

    auto entity_count() const noexcept -> std::size_t {
        return entities_.size();
    }

    auto create_entity(
        ResourceId<Model> model,
        Transform transform
    ) -> Entity&;
    auto clear_entities() noexcept -> void;

private:
    Camera camera_;
    std::vector<Entity> entities_;
    std::vector<PointLight> point_lights_;
};

#endif
