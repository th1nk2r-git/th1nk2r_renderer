#include "io/model_loader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace {
    constexpr unsigned int import_flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_PreTransformVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_FlipUVs;

    auto checked_index_count(const aiMesh& source) -> size_t {
        constexpr auto indices_per_triangle = size_t{3};
        constexpr auto max_size = std::numeric_limits<size_t>::max();

        if (source.mNumFaces > max_size / indices_per_triangle) {
            throw std::length_error(
                "imported mesh index count exceeds size_t"
            );
        }

        return static_cast<size_t>(source.mNumFaces) * indices_per_triangle;
    }

    auto convert_mesh(const aiMesh& source) -> MeshData {
        if (!source.HasPositions() || source.mNumVertices == 0) {
            throw std::runtime_error(
                "imported mesh does not contain vertex positions"
            );
        }

        MeshData result{};
        result.material_index_ = source.mMaterialIndex;
        result.vertices_.reserve(source.mNumVertices);
        result.indices_.reserve(checked_index_count(source));

        const bool has_colors = source.HasVertexColors(0);
        const bool has_texcoords = source.HasTextureCoords(0);

        for (unsigned int i = 0; i < source.mNumVertices; ++i) {
            const auto& position = source.mVertices[i];

            Vertex vertex{};
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;

            if (has_colors) {
                const auto& color = source.mColors[0][i];
                vertex.color[0] = color.r;
                vertex.color[1] = color.g;
                vertex.color[2] = color.b;
            } else {
                vertex.color[0] = 1.0F;
                vertex.color[1] = 1.0F;
                vertex.color[2] = 1.0F;
            }

            if (has_texcoords) {
                const auto& texcoord = source.mTextureCoords[0][i];
                vertex.texcoord[0] = texcoord.x;
                vertex.texcoord[1] = texcoord.y;
            }

            result.vertices_.push_back(vertex);
        }

        for (unsigned int i = 0; i < source.mNumFaces; ++i) {
            const auto& face = source.mFaces[i];
            if (face.mNumIndices != 3 || face.mIndices == nullptr) {
                throw std::runtime_error(
                    "imported mesh contains a non-triangle face"
                );
            }

            result.indices_.push_back(face.mIndices[0]);
            result.indices_.push_back(face.mIndices[1]);
            result.indices_.push_back(face.mIndices[2]);
        }

        if (result.indices_.empty()) {
            throw std::runtime_error(
                "imported mesh does not contain triangle indices"
            );
        }

        return result;
    }
}

auto load_model(const std::filesystem::path& path) -> ModelData {
    if (path.empty()) {
        throw std::invalid_argument("model path cannot be empty");
    }

    Assimp::Importer importer;
    importer.SetPropertyInteger(
        AI_CONFIG_PP_SBP_REMOVE,
        aiPrimitiveType_POINT | aiPrimitiveType_LINE
    );

    const auto path_string = path.string();
    const aiScene* scene = importer.ReadFile(path_string, import_flags);

    if (scene == nullptr) {
        throw std::runtime_error(
            "failed to load model '" + path_string + "': " +
            importer.GetErrorString()
        );
    }

    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr) {
        throw std::runtime_error(
            "imported model scene is incomplete: " + path_string
        );
    }

    if (scene->mNumMeshes == 0 || scene->mMeshes == nullptr) {
        throw std::runtime_error(
            "imported model does not contain triangle meshes: " +
            path_string
        );
    }

    ModelData result{};
    result.meshes_.reserve(scene->mNumMeshes);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* source = scene->mMeshes[i];
        if (source == nullptr) {
            throw std::runtime_error(
                "imported model contains a null mesh: " + path_string
            );
        }

        result.meshes_.push_back(convert_mesh(*source));
    }

    return result;
}
