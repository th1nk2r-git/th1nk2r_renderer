#include "utils/io/model_loader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include "utils/io/image_loader.hpp"

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

    auto convert_embedded_texture(const aiTexture& source) -> ImageData {
        if (source.pcData == nullptr || source.mWidth == 0) {
            throw std::runtime_error(
                "imported material contains an empty embedded texture"
            );
        }

        if (source.mHeight == 0) {
            return load_image_rgba8(
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(source.pcData),
                    static_cast<size_t>(source.mWidth)
                }
            );
        }

        const auto width = static_cast<size_t>(source.mWidth);
        const auto height = static_cast<size_t>(source.mHeight);
        constexpr auto channel_count = size_t{4};
        constexpr auto max_size = std::numeric_limits<size_t>::max();

        if (width > max_size / height ||
            width * height > max_size / channel_count) {
            throw std::length_error(
                "embedded texture dimensions exceed size_t"
            );
        }

        ImageData result{
            .width = source.mWidth,
            .height = source.mHeight,
            .channels = static_cast<uint32_t>(channel_count),
            .pixels = std::vector<std::byte>(
                width * height * channel_count
            )
        };

        const auto texel_count = width * height;
        for (size_t i = 0; i < texel_count; ++i) {
            const auto& texel = source.pcData[i];
            const auto offset = i * channel_count;
            result.pixels[offset + 0] = static_cast<std::byte>(texel.r);
            result.pixels[offset + 1] = static_cast<std::byte>(texel.g);
            result.pixels[offset + 2] = static_cast<std::byte>(texel.b);
            result.pixels[offset + 3] = static_cast<std::byte>(texel.a);
        }

        return result;
    }

    auto load_base_color_texture(
        const aiMaterial& source,
        const aiScene& scene,
        const std::filesystem::path& model_path
    ) -> std::optional<ImageData> {
        aiString texture_path;
        auto result = source.GetTexture(
            aiTextureType_BASE_COLOR,
            0,
            &texture_path
        );
        if (result != AI_SUCCESS) {
            result = source.GetTexture(
                aiTextureType_DIFFUSE,
                0,
                &texture_path
            );
        }
        if (result != AI_SUCCESS || texture_path.length == 0) {
            return std::nullopt;
        }

        if (const auto* embedded =
                scene.GetEmbeddedTexture(texture_path.C_Str())) {
            return convert_embedded_texture(*embedded);
        }

        auto resolved_path = std::filesystem::path{texture_path.C_Str()};
        if (resolved_path.is_relative()) {
            resolved_path = model_path.parent_path() / resolved_path;
        }
        return load_image_rgba8(resolved_path.lexically_normal());
    }

    auto convert_material(
        const aiMaterial& source,
        const aiScene& scene,
        const std::filesystem::path& model_path
    ) -> MaterialData {
        aiColor4D base_color{1.0F, 1.0F, 1.0F, 1.0F};
        if (source.Get(AI_MATKEY_BASE_COLOR, base_color) != AI_SUCCESS) {
            static_cast<void>(
                source.Get(AI_MATKEY_COLOR_DIFFUSE, base_color)
            );
        }

        return MaterialData{
            .base_color_ = {
                base_color.r,
                base_color.g,
                base_color.b,
                base_color.a
            },
            .base_color_texture_ = load_base_color_texture(
                source,
                scene,
                model_path
            )
        };
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
    if (scene->mNumMaterials == 0 || scene->mMaterials == nullptr) {
        result.material_.emplace_back();
    }
    else {
        result.material_.reserve(scene->mNumMaterials);
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
            const auto* source = scene->mMaterials[i];
            if (source == nullptr) {
                throw std::runtime_error(
                    "imported model contains a null material: " +
                    path_string
                );
            }
            result.material_.push_back(
                convert_material(*source, *scene, path)
            );
        }
    }

    result.meshes_.reserve(scene->mNumMeshes);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* source = scene->mMeshes[i];
        if (source == nullptr) {
            throw std::runtime_error(
                "imported model contains a null mesh: " + path_string
            );
        }

        if (source->mMaterialIndex >= result.material_.size()) {
            throw std::runtime_error(
                "imported mesh material index is out of range: " +
                path_string
            );
        }

        result.meshes_.push_back(convert_mesh(*source));
    }

    return result;
}
