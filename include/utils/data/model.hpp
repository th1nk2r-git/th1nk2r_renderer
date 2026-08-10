#ifndef MODEL_DATA_HPP
#define MODEL_DATA_HPP

#include <vector>

#include "utils/data/mesh.hpp"
#include "utils/data/material.hpp"

struct ModelData {
    std::vector<MaterialData> material_;
    std::vector<MeshData> meshes_;
};

#endif
