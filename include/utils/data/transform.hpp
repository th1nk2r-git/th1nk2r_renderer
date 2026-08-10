#ifndef TRANSFORM_HPP
#define TRANSFORM_HPP

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

struct Transform {
    glm::vec3 position{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
};

#endif