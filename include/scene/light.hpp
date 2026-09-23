#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <glm/vec3.hpp>

struct PointLight {
    // World-space position.
    glm::vec3 position{0.0F};
    float range{5000.0F};
    float radiance{1.0};

    // Linear RGB color.
    glm::vec3 color{1.0F};
    float intensity{100.0F};
};

#endif
