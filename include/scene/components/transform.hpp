#ifndef SCENE_COMPONENTS_TRANSFORM_HPP
#define SCENE_COMPONENTS_TRANSFORM_HPP

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "scene/component.hpp"

class Transform final : public Component {
public:
    Transform() = default;
    Transform(
        glm::vec3 position,
        glm::quat rotation,
        glm::vec3 scale
    ) noexcept;

    auto position() noexcept -> glm::vec3& {
        return position_;
    }

    auto position() const noexcept -> const glm::vec3& {
        return position_;
    }

    auto rotation() noexcept -> glm::quat& {
        return rotation_;
    }

    auto rotation() const noexcept -> const glm::quat& {
        return rotation_;
    }

    auto scale() noexcept -> glm::vec3& {
        return scale_;
    }

    auto scale() const noexcept -> const glm::vec3& {
        return scale_;
    }

    auto model_matrix() const noexcept -> glm::mat4;

private:
    glm::vec3 position_{0.0F};
    glm::quat rotation_{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale_{1.0F};
};

#endif
