#ifndef MESH_HPP
#define MESH_HPP

#include <cstdint>
#include <glm/vec3.hpp>

struct Mesh {
    struct Bounds {
        glm::vec3 minimum{0.0F};
        glm::vec3 maximum{0.0F};
    };

    // Offsets are measured in vertices/indices, not bytes. Indices stay local.
    int32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    Bounds bounds;
};

#endif
