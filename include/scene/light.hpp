#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <glm/vec3.hpp>

struct PointLight {
    // World-space position.
    glm::vec3 position{0.0F};
    float range{10.0F};

    // Linear RGB color.
    glm::vec3 color{1.0F};
    float intensity{100.0F};
};

#endif
