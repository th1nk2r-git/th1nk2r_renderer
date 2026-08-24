#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <glm/vec3.hpp>

struct PointLight {
    glm::vec3 position{0.0F};
    glm::vec3 color{1.0F};
    float intensity{1.0F};
    bool casts_shadow{true};
    float shadow_near{0.1F};
    float shadow_far{25.0F};
    float source_radius{0.15F};
};

#endif
