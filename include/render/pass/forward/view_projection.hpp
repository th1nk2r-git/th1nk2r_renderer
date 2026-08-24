#ifndef VIEW_PROJECTION_HPP
#define VIEW_PROJECTION_HPP

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

struct alignas(16) ViewProjection {
    glm::mat4 view {1.0F};
    glm::mat4 projection {1.0F};
    glm::vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
};

#endif
