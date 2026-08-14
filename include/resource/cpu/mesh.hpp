#ifndef MESH_DATA_HPP
#define MESH_DATA_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

struct Vertex {
    float position[3];
    float color[3];
    float texcoord[2];
};

struct MeshData {
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::size_t material_index_ = 0;
};

#endif
