#ifndef MODEL_DATA_HPP
#define MODEL_DATA_HPP

#include <vector>

#include "resource/cpu/material.hpp"
#include "resource/cpu/mesh.hpp"

struct ModelData {
    std::vector<MaterialData> material_;
    std::vector<MeshData> meshes_;
};

#endif
