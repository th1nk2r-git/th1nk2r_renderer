#ifndef MESH_HPP
#define MESH_HPP

#include <vector>
#include "geometry/vertex.hpp"

struct Mesh {
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
};

#endif
