#ifndef MODEL_DATA_HPP
#define MODEL_DATA_HPP

#include <vector>

#include "resource/cpu/material.hpp"
#include "resource/cpu/mesh.hpp"
#include "resource/cpu/texture.hpp"

struct ModelData {
    std::vector<TextureData> textures_;
    std::vector<MaterialData> material_;
    std::vector<MeshData> meshes_;
};

#endif
